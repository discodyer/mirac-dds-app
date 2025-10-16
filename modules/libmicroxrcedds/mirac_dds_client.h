// Copyright (c) 2025, Cody Gu <gujiaqi@iscas.ac.cn>
// SPDX-License-Identifier: Apache-2.0

#ifndef MIRAC_DDS_CLIENT_H_
#define MIRAC_DDS_CLIENT_H_

#include <cstdint>
#include <cstddef>
#include <string.h>
#include <uxr/client/client.h>
#include "builtin_interfaces/msg/Time.h"
#include "std_msgs/msg/String.h"

#define ROS_DDS_MSG_TYPE_NAME(pkg, type) pkg "::msg::dds_::" type "_"
#define ROS_DDS_TOPIC_NAME(topic) "rt" topic
#define ROS_DDS_TOPIC_NAMESPACE(namespace, topic) ROS_DDS_TOPIC_NAME("/" namespace "/" topic)

#define DEBUG_MSG_PREFIX(log_level) "[UXRCE-DDS]" log_level ":"
#define DEBUG_MSG_PREFIX_DEBUG DEBUG_MSG_PREFIX("DEBUG")
#define DEBUG_MSG_PREFIX_INFO DEBUG_MSG_PREFIX("INFO")
#define DEBUG_MSG_PREFIX_WARN DEBUG_MSG_PREFIX("WARNING")
#define DEBUG_MSG_PREFIX_ERROR DEBUG_MSG_PREFIX("ERROR")

class MiracDDS
{
public:
    // This will config in Kconfig
    inline static constexpr uint16_t DDS_STREAM_HISTORY = 20;
    inline static constexpr size_t DDS_BUFFER_SIZE = UXR_CONFIG_CUSTOM_TRANSPORT_MTU * DDS_STREAM_HISTORY;
    inline static constexpr int DDS_REQ_TIMEOUT_MS = 500;
    inline static constexpr uint32_t ROS_DOMAIN_ID = 0; // DDS domain ID
    // Maximum number of attempts to ping the XRCE agent before exiting
    inline static constexpr uint8_t DDS_PING_MAX_RETRY = 10;
    // Timeout in milliseconds when pinging the XRCE agent
    inline static constexpr int DDS_PING_TIMEOUT_MS = 1000;
    inline static constexpr uint16_t DDS_PARTICIPANT_ID = 0x01;

#if defined(CONFIG_MICROXRCEDDSCLIENT_PARTICIPANT_NAME)
    inline static constexpr const char *DDS_PARTICIPANT_NAME = CONFIG_MICROXRCEDDSCLIENT_PARTICIPANT_NAME;
#else
    inline static constexpr const char *DDS_PARTICIPANT_NAME = "microxrcedds_participant";
#endif

public:
    MiracDDS();
    ~MiracDDS();

    // Start DDS thread
    bool startThread();

    // Main loop
    void mainLoop();

    // Initialize transport
    bool initTransport();

    // Initialize session
    bool initSession();

    // Create participant and topics
    bool createEntities();

    // Handle communication
    bool spinOnce(int timeout_ms = 1);

    // Update internal data and publish
    void update();

    // Check connection status
    bool isConnected() const;

    // Clean up resources
    void cleanup();

public:
    enum class topicRole : uint8_t
    {
        TOPIC_ROLE_PUB = 0,
        TOPIC_ROLE_SUB = 1,
    };

    struct topicList
    {
        const uxrObjectId topic_id;       // DDS topic ID
        const topicRole role_type;        // Whether publisher or subscriber
        const uxrObjectId role_id;        // Publisher/Subscriver ID
        const uxrObjectId data_entity_id; // Data writer/reader ID
        const char *topic_name;           // DDS Topic name
        const char *type_name;            // Message type
        const uint32_t rate_limit;        // Rate limit
        const uxrQoS_t qos;               // QoS
    };
    static const topicList topics[]; // Custom topic list

private:
    static void on_topic_entry(uxrSession *uxr_session, uxrObjectId object_id, uint16_t request_id, uxrStreamId stream_id, struct ucdrBuffer *ub, uint16_t length, void *args);
    void on_topic(uxrSession* session, uxrObjectId object_id, uint16_t request_id, uxrStreamId stream_id, struct ucdrBuffer* ub, uint16_t length);

    // Topic publishing methods
    bool writeTopicTalker();

    // Data update methods
    static void updateTopic(std_msgs_msg_String *msg, const char *str);
    void updateTopic(builtin_interfaces_msg_Time *msg);

private:
    uxrSession session_;

    // client key we present
    static constexpr uint32_t client_key = 0xAAAABBBB;

    // delivery control params
    uxrDeliveryControl delivery_control{
        UXR_MAX_SAMPLES_UNLIMITED, // max_samples
        0,                         // max_elapsed_time
        0,                         // max_bytes_per_second
        0                          // min_pace_period
    };

    uxrCustomTransport transport_;
    bool is_status_ok_{false};
    bool is_connected_{false};

    uxrStreamId reliable_out_;
    uxrStreamId reliable_in_;
    uint8_t output_buffer_[DDS_BUFFER_SIZE] __aligned(4);
    uint8_t input_buffer_[DDS_BUFFER_SIZE] __aligned(4);

    int64_t last_time_syncd_time_ms_{0};

    std_msgs_msg_String talker_topic_;
    int64_t last_talker_time_ms_{0};

    std_msgs_msg_String rx_chatter_topic_;
};

#endif // MIRAC_DDS_CLIENT_H_