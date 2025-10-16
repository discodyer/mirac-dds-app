// Copyright (c) 2025, Cody Gu <gujiaqi@iscas.ac.cn>
// SPDX-License-Identifier: Apache-2.0

#ifndef UXRCE_DDS_TOPIC_LIST_H_
#define UXRCE_DDS_TOPIC_LIST_H_

#include "mirac_dds_client.h"
#include <cstdint>
#include <uxr/client/client.h>

#define DDS_DELAY_TIME_SYNC_MS (60000)
#define DDS_DELAY_TALKER_TOPIC_MS (1000)

#define TOPIC_NS CONFIG_MICROXRCEDDSCLIENT_ROS_TOPIC_NAMESPACE

// Explicit conversion to uint8_t

enum class TopicIndex : uint8_t
{
    TALKER_PUB = 0,
    CHATTER_SUB,
};

static inline constexpr uint8_t to_underlying(const TopicIndex index)
{
    static_assert(sizeof(index) == sizeof(uint8_t));
    return static_cast<uint8_t>(index);
}

constexpr struct MiracDDS::topicList MiracDDS::topics[] = {
    {
        .topic_id = (uxrObjectId){.id = to_underlying(TopicIndex::TALKER_PUB), .type = UXR_TOPIC_ID},
        .role_type = topicRole::TOPIC_ROLE_PUB,
        .role_id = (uxrObjectId){.id = to_underlying(TopicIndex::TALKER_PUB), .type = UXR_PUBLISHER_ID},
        .data_entity_id = (uxrObjectId){.id = to_underlying(TopicIndex::TALKER_PUB), .type = UXR_DATAWRITER_ID},
        .topic_name = ROS_DDS_TOPIC_NAMESPACE(TOPIC_NS, "HelloWorld"),
        .type_name = ROS_DDS_MSG_TYPE_NAME("std_msgs", "String"),
        .qos = (uxrQoS_t){
            .durability = UXR_DURABILITY_VOLATILE,
            .reliability = UXR_RELIABILITY_RELIABLE,
            .history = UXR_HISTORY_KEEP_LAST,
            .depth = 5,
        },
    },
    {
        .topic_id = (uxrObjectId){.id = to_underlying(TopicIndex::CHATTER_SUB), .type = UXR_TOPIC_ID},
        .role_type = topicRole::TOPIC_ROLE_SUB,
        .role_id = (uxrObjectId){.id = to_underlying(TopicIndex::CHATTER_SUB), .type = UXR_SUBSCRIBER_ID},
        .data_entity_id = (uxrObjectId){.id=to_underlying(TopicIndex::CHATTER_SUB), .type=UXR_DATAREADER_ID},
        .topic_name = ROS_DDS_TOPIC_NAMESPACE(TOPIC_NS, "chatter"),
        .type_name = ROS_DDS_MSG_TYPE_NAME("std_msgs", "String"),
        .qos = (uxrQoS_t){
            .durability = UXR_DURABILITY_VOLATILE,
            .reliability = UXR_RELIABILITY_RELIABLE,
            .history = UXR_HISTORY_KEEP_LAST,
            .depth = 5,
        },
    },
};

#endif // UXRCE_DDS_TOPIC_LIST_H_