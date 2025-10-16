// Copyright (c) 2025, Cody Gu <gujiaqi@iscas.ac.cn>
// SPDX-License-Identifier: Apache-2.0

#include <uxr/client/util/ping.h>
#include <uxr/client/util/time.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include "microxrce_transports.h"

#include "mirac_dds_frames.h"
#include "mirac_dds_client.h"
#include "mirac_dds_topic_list.h"

LOG_MODULE_REGISTER(DDS, LOG_LEVEL_INF);

static zephyr_transport_params_t default_params;

// Thread stack and thread control block
#define DDS_THREAD_STACK_SIZE 8192
K_THREAD_STACK_DEFINE(dds_thread_stack, DDS_THREAD_STACK_SIZE);
static struct k_thread dds_thread_data;

// Thread static entry wrapper
static void miracdds_thread_entry(void *p1, void *p2, void *p3)
{
    MiracDDS *self = reinterpret_cast<MiracDDS *>(p1);
    if (self)
    {
        self->mainLoop();
    }
}

MiracDDS::MiracDDS()
    : session_{}, transport_{}, is_status_ok_{false}, is_connected_{false}, 
      reliable_out_{}, reliable_in_{},
      last_time_syncd_time_ms_{0},
      talker_topic_{}, last_talker_time_ms_{0},
      rx_chatter_topic_{}
{
    // Initialize message defaults if needed
    talker_topic_.data[0] = '\0';
}

MiracDDS::~MiracDDS()
{
    cleanup();
}

//---------------------------------------------------------------------
// Public API
//---------------------------------------------------------------------

bool MiracDDS::startThread()
{
    // Create Zephyr thread
    k_tid_t tid = k_thread_create(&dds_thread_data,
                                  dds_thread_stack,
                                  K_THREAD_STACK_SIZEOF(dds_thread_stack),
                                  miracdds_thread_entry,
                                  this, nullptr, nullptr,
                                  CONFIG_MAIN_THREAD_PRIORITY + 1,
                                  0,
                                  K_NO_WAIT);
    if (!tid)
    {
        LOG_ERR("Failed to create DDS thread.");
        return false;
    }

    LOG_INF("DDS thread started.");
    return true;
}

void MiracDDS::mainLoop()
{
    LOG_INF("DDS Client initializing transport.");
    if (!initTransport())
    {
        return;
    }

    while (true)
    {
        if (!uxr_ping_agent_attempts(&transport_.comm, DDS_PING_TIMEOUT_MS, DDS_PING_MAX_RETRY))
        {
            LOG_WRN("No ping response, retrying.");
            continue;
        }

        if (!initSession() || !createEntities())
        {
            LOG_ERR("Session init requests failed.");
            return;
        }
        is_connected_ = true;
        LOG_INF("DDS Client Initialization Good.");

        int64_t cur_time_ms = uxr_millis();

        int64_t last_ping_ms = 0;
        size_t num_pings_missed = 0;
        bool had_ping_reply = false;

        uxr_sync_session(&session_, DDS_REQ_TIMEOUT_MS);
        last_time_syncd_time_ms_ = cur_time_ms;
        LOG_DBG("Time synchronized. offset: %" PRId64 " us", session_.time_offset / 1000);
        while (isConnected())
        {
            // Reduce sleep to allow higher frequency operation
            // Use microsecond delay for better timing precision
            k_usleep(100);

            // publish topics
            update();

            // check ping response
            if (session_.on_pong_flag == PONG_IN_SESSION_STATUS)
            {
                had_ping_reply = true;
            }

            cur_time_ms = uxr_millis();
            if (cur_time_ms - last_ping_ms > DDS_REQ_TIMEOUT_MS)
            {
                last_ping_ms = cur_time_ms;

                if (had_ping_reply)
                {
                    num_pings_missed = 0;
                }
                else
                {
                    ++num_pings_missed;
                }

                const int ping_agent_timeout_ms = 0;
                const uint8_t ping_agent_attempts = 1;
                uxr_ping_agent_session(&session_, ping_agent_timeout_ms, ping_agent_attempts);

                had_ping_reply = false;
            }

            if (num_pings_missed > 2)
            {
                LOG_ERR("No ping response, disconnecting.");
                is_connected_ = false;
            }
        }

        cleanup();
        LOG_INF("DDS Client closed.");
    }
}

bool MiracDDS::initTransport()
{
    // Initialize Zephyr custom transport
    uxr_set_custom_transport_callbacks(&transport_,
                                       true,
                                       zephyr_transport_open,
                                       zephyr_transport_close,
                                       zephyr_transport_write,
                                       zephyr_transport_read);

    if (!uxr_init_custom_transport(&transport_, &default_params))
    {
        LOG_ERR("Transport initialization failed.");
        return false;
    }
    return true;
}

bool MiracDDS::initSession()
{
    uxr_init_session(&session_, &transport_.comm, client_key);

    // Register topic callbacks
    uxr_set_topic_callback(&session_, MiracDDS::on_topic_entry, this);

    if (!uxr_create_session(&session_))
    {
        LOG_ERR("Error at create session.");
        return false;
    }

    memset(output_buffer_, 0, DDS_BUFFER_SIZE);
    memset(input_buffer_, 0, DDS_BUFFER_SIZE);

    reliable_out_ = uxr_create_output_reliable_stream(
        &session_, output_buffer_, DDS_BUFFER_SIZE, DDS_STREAM_HISTORY);

    reliable_in_ = uxr_create_input_reliable_stream(
        &session_, input_buffer_, DDS_BUFFER_SIZE, DDS_STREAM_HISTORY);

    LOG_INF("Session init complete.");
    return true;
}

bool MiracDDS::createEntities()
{
    // Create Participant
    const uxrObjectId participant_id = uxr_object_id(DDS_PARTICIPANT_ID, UXR_PARTICIPANT_ID);
    const uint16_t participant_req = uxr_buffer_create_participant_bin(&session_, reliable_out_, participant_id,
                                                                       ROS_DOMAIN_ID, DDS_PARTICIPANT_NAME, UXR_REPLACE);

    const uint16_t request_participant[1] = {participant_req};
    uint8_t status_participant[1] = {0};
    if (!uxr_run_session_until_all_status(&session_, DDS_REQ_TIMEOUT_MS, request_participant, status_participant, 1))
    {
        LOG_ERR("Participant session request failure.");
        return false;
    }

    const size_t topics_count = sizeof(topics) / sizeof(topics[0]);
    LOG_DBG("Topics Count = %u", (unsigned)topics_count);

    for (size_t i = 0; i < topics_count; ++i)
    {
        const auto &t = topics[i];

        // Create Topic
        const uint16_t topic_req_id =
            uxr_buffer_create_topic_bin(&session_, reliable_out_, t.topic_id,
                                        participant_id, t.topic_name, t.type_name, UXR_REPLACE);

        // Create Publisher / Subscriber
        const uint16_t role_req_id =
            (t.role_type == topicRole::TOPIC_ROLE_PUB)
                ? uxr_buffer_create_publisher_bin(&session_, reliable_out_, t.role_id, participant_id, UXR_REPLACE)
                : uxr_buffer_create_subscriber_bin(&session_, reliable_out_, t.role_id, participant_id, UXR_REPLACE);

        // Create DataWriter / DataReader
        const uint16_t data_entity_req_id =
            (t.role_type == topicRole::TOPIC_ROLE_PUB)
                ? uxr_buffer_create_datawriter_bin(&session_, reliable_out_, t.data_entity_id, t.role_id, t.topic_id, t.qos, UXR_REPLACE)
                : uxr_buffer_create_datareader_bin(&session_, reliable_out_, t.data_entity_id, t.role_id, t.topic_id, t.qos, UXR_REPLACE);

        const uint16_t requests[3] = {topic_req_id, role_req_id, data_entity_req_id};
        uint8_t status[3] = {0, 0, 0};

        if (!uxr_run_session_until_all_status(&session_, DDS_REQ_TIMEOUT_MS, requests, status, 3))
        {
            LOG_ERR("Topic/%s/%s session request failure for index '%u'",
                    (t.role_type == topicRole::TOPIC_ROLE_PUB) ? "Pub" : "Sub",
                    (t.role_type == topicRole::TOPIC_ROLE_PUB) ? "Writer" : "Reader",
                    (unsigned)i);
            LOG_ERR("Status 'Topic' result '%u'", status[0]);
            LOG_ERR("Status '%s' result '%u'",
                    (t.role_type == topicRole::TOPIC_ROLE_PUB) ? "Pub" : "Sub",
                    status[1]);
            LOG_ERR("Status 'Data %s' result '%u'",
                    (t.role_type == topicRole::TOPIC_ROLE_PUB) ? "Writer" : "Reader",
                    status[2]);
            return false;
        }
        else
        {
            LOG_DBG("Topic/%s/%s session pass for index '%u'",
                    (t.role_type == topicRole::TOPIC_ROLE_PUB) ? "Pub" : "Sub",
                    (t.role_type == topicRole::TOPIC_ROLE_PUB) ? "Writer" : "Reader",
                    (unsigned)i);
            if (t.role_type == topicRole::TOPIC_ROLE_SUB) uxr_buffer_request_data(&session_, reliable_out_, t.data_entity_id, reliable_in_, &delivery_control);
        }
    }

    LOG_INF("Client initialized successfully.");
    return true;
}

bool MiracDDS::spinOnce(int timeout_ms)
{
    is_status_ok_ = uxr_run_session_time(&session_, timeout_ms);
    return is_status_ok_;
}

void MiracDDS::update()
{
    const int64_t cur_time_ms = uxr_epoch_millis(&session_);

    if (cur_time_ms - last_time_syncd_time_ms_ > DDS_DELAY_TIME_SYNC_MS)
    {
        uxr_sync_session(&session_, DDS_REQ_TIMEOUT_MS);
        last_time_syncd_time_ms_ = cur_time_ms;
        LOG_DBG("Time synchronized. offset: %" PRId64 " us", session_.time_offset / 1000);
    }

    if (cur_time_ms - last_talker_time_ms_ > DDS_DELAY_TALKER_TOPIC_MS)
    {
        const char msg[] = "Hello from Zephyr!";
        updateTopic(&talker_topic_, msg);
        last_talker_time_ms_ = cur_time_ms;
        // LOG_DBG("Publish string_topic: %s", string_topic_.data);
        (void)writeTopicTalker();
    }

    spinOnce(1);
}

bool MiracDDS::isConnected() const
{
    return is_connected_;
}

void MiracDDS::cleanup()
{
    if (is_connected_)
    {
        uxr_delete_session(&session_);
        uxr_close_custom_transport(&transport_);
        is_connected_ = false;
        LOG_INF("Client resources cleaned up.");
    }
}

//---------------------------------------------------------------------
// Private: topic publishing
//---------------------------------------------------------------------

bool MiracDDS::writeTopicTalker()
{
    if (!isConnected())
    {
        return false;
    }

    ucdrBuffer ub{};
    const uint32_t topic_size = std_msgs_msg_String_size_of_topic(&talker_topic_, 0);
    if (!uxr_prepare_output_stream(
            &session_, reliable_out_, topics[to_underlying(TopicIndex::TALKER_PUB)].data_entity_id, &ub, topic_size))
    {
        LOG_ERR("Failed to prepare output stream.");
        return false;
    }

    const bool ok = std_msgs_msg_String_serialize_topic(&ub, &talker_topic_);
    if (!ok)
    {
        LOG_ERR("Failed to serialize std_msgs::String.");
        return false;
    }
    return true;
}

//---------------------------------------------------------------------
// Private: data update helpers
//---------------------------------------------------------------------

void MiracDDS::updateTopic(std_msgs_msg_String *msg, const char *str)
{
    if (!msg || !str)
        return;
    strncpy(msg->data, str, sizeof(msg->data) - 1);
    msg->data[sizeof(msg->data) - 1] = '\0';
}

void MiracDDS::updateTopic(builtin_interfaces_msg_Time *msg)
{
    if (!msg)
        return;

    int64_t utc_nanos = uxr_epoch_nanos(&session_);
    msg->sec = static_cast<int32_t>(utc_nanos / 1000000000ULL);
    msg->nanosec = static_cast<uint32_t>(utc_nanos % 1000000000ULL);
}

void MiracDDS::on_topic_entry(uxrSession *uxr_session, uxrObjectId object_id, uint16_t request_id, uxrStreamId stream_id, struct ucdrBuffer *ub, uint16_t length,
                              void *args)
{
    MiracDDS *dds = (MiracDDS *)args;
    dds->on_topic(uxr_session, object_id, request_id, stream_id, ub, length);
}

void MiracDDS::on_topic(uxrSession *uxr_session, uxrObjectId object_id, uint16_t request_id, uxrStreamId stream_id, struct ucdrBuffer *ub, uint16_t length)
{
    (void)uxr_session;
    (void)request_id;
    (void)stream_id;
    (void)length;

    switch (object_id.id)
    {
    case topics[to_underlying(TopicIndex::CHATTER_SUB)].data_entity_id.id:
    {
        const bool success = std_msgs_msg_String_deserialize_topic(ub, &rx_chatter_topic_);
        if (success == false)
        {
            LOG_ERR("Failed to deserialize a String msg.");
            break;
        }
        LOG_INF("I heard: %s", rx_chatter_topic_.data);
        break;
    }
    }
}
