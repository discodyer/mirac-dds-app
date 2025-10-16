# mirac-dds-app

MiracDDS example on zephyr with micro-XRCE-DDS

## 安装并使用 microxrceddsgen 生成消息源文件

https://micro-xrce-dds.docs.eprosima.com/en/latest/installation.html

根据上面的链接描述，编译安装`microxrceddsgen`

使用 `mirac-dds-app\modules\libmicroxrcedds\generate_dds_messages.py` 脚本批量生成，需要先在目录下创建一个解析消息列表文件

```text
    解析消息列表文件
    格式: <package>/<msg_type> (每行一个)
    示例:
        sensor_msgs/Imu
        std_msgs/Header
```

将生成的头文件和源文件复制到 `mirac-dds-app\modules\libmicroxrcedds\uxrce_generated_msgs` 下

## 添加发布话题

首先编辑 `mirac-dds-app\modules\libmicroxrcedds\mirac_dds_topic_list.h` 添加下面的内容

在`enum class TopicIndex`中添加新的消息名称，比如`TEST_PUB`

然后在`constexpr struct MiracDDS::topicList MiracDDS::topics[]`中严格按照`TopicIndex`的顺序添加描述配置

```c
{
    .topic_id = (uxrObjectId){.id = to_underlying(TopicIndex::TEST_PUB), .type = UXR_TOPIC_ID},
    .role_type = topicRole::TOPIC_ROLE_PUB,
    .role_id = (uxrObjectId){.id = to_underlying(TopicIndex::TEST_PUB), .type = UXR_PUBLISHER_ID},
    .data_entity_id = (uxrObjectId){.id = to_underlying(TopicIndex::TEST_PUB), .type = UXR_DATAWRITER_ID},
    .topic_name = ROS_DDS_TOPIC_NAMESPACE(TOPIC_NS, "test"), // 在ros下会变成话题/miracdds/test
    .type_name = ROS_DDS_MSG_TYPE_NAME("std_msgs", "String"), // 使用std_msgs/String消息类型，也可以使用自定义消息类型并使用microxrceddsgen生成对应的头文件
    .qos = (uxrQoS_t){ // QoS设置
        .durability = UXR_DURABILITY_VOLATILE,
        .reliability = UXR_RELIABILITY_RELIABLE, // 或者UXR_RELIABILITY_BEST_EFFORT
        .history = UXR_HISTORY_KEEP_LAST,
        .depth = 5,
    },
},
```

然后定义话题发布的时间间隔宏，如果需要自动发送

```c
#define DDS_DELAY_TEST_TOPIC_MS (1000) // 1秒重复一次
```

然后进入文件`mirac-dds-app\modules\libmicroxrcedds\mirac_dds_client.h`并添加以下内容

添加成员变量

```c
    std_msgs_msg_String test_topic_;
    int64_t last_test_time_ms_{0};
```

添加private方法

```c
    static void updateTopic(std_msgs_msg_String *msg, const char *str); // std_msgs/String的话题更新方法
    // 如果使用自定义消息类型，可以自定义对应的话题更新方法
    // void updateTopic(custom_msgs_msg_Custom *msg, args); 

    bool writeTopicTest(); // 将话题写入缓冲区
```

进入文件`mirac-dds-app\modules\libmicroxrcedds\mirac_dds_client.cpp`并添加以下内容

初始化成员变量

```c
MiracDDS::MiracDDS()
    : session_{}, transport_{}, is_status_ok_{false}, is_connected_{false}, 
      reliable_out_{}, reliable_in_{},
      last_time_syncd_time_ms_{0},
      talker_topic_{}, last_talker_time_ms_{0}, // 添加这个初始化
      rx_chatter_topic_{}
{
    talker_topic_.data[0] = '\0'; // 添加这个初始化
}
```

在`void MiracDDS::update()`方法中添加下面内容

```c
    ···

    if (cur_time_ms - last_test_time_ms_ > DDS_DELAY_TEST_TOPIC_MS)
    {
        const char msg[] = "Hello from Zephyr!";
        updateTopic(&test_topic_, msg);
        last_ttest_time_ms_ = cur_time_ms;
        (void)writeTopicTest();
    }

    ···
```

## 添加订阅话题

首先编辑 `mirac-dds-app\modules\libmicroxrcedds\mirac_dds_topic_list.h` 添加下面的内容

在`enum class TopicIndex`中添加新的消息名称，比如`TEST_SUB`

然后在`constexpr struct MiracDDS::topicList MiracDDS::topics[]`中严格按照`TopicIndex`的顺序添加描述配置

```c
{
    .topic_id = (uxrObjectId){.id = to_underlying(TopicIndex::TEST_SUB), .type = UXR_TOPIC_ID},
    .role_type = topicRole::TOPIC_ROLE_SUB,
    .role_id = (uxrObjectId){.id = to_underlying(TopicIndex::TEST_SUB), .type = UXR_SUBSCRIBER_ID},
    .data_entity_id = (uxrObjectId){.id=to_underlying(TopicIndex::TEST_SUB), .type=UXR_DATAREADER_ID},
    .topic_name = ROS_DDS_TOPIC_NAMESPACE(TOPIC_NS, "chatter"), // 在ros下会变成话题/miracdds/chatter
    .type_name = ROS_DDS_MSG_TYPE_NAME("std_msgs", "String"), // 使用std_msgs/String消息类型，也可以使用自定义消息类型并使用microxrceddsgen生成对应的头文件
    .qos = (uxrQoS_t){ // QoS设置
        .durability = UXR_DURABILITY_VOLATILE,
        .reliability = UXR_RELIABILITY_RELIABLE, // 或者UXR_RELIABILITY_BEST_EFFORT
        .history = UXR_HISTORY_KEEP_LAST,
        .depth = 5,
    },
},
```

在`mirac-dds-app\modules\libmicroxrcedds\mirac_dds_client.h`中添加下面的私有成员定义

```c
    std_msgs_msg_String rx_chatter_topic_;
```

初始化成员变量

```c
MiracDDS::MiracDDS()
    : session_{}, transport_{}, is_status_ok_{false}, is_connected_{false}, 
      reliable_out_{}, reliable_in_{},
      last_time_syncd_time_ms_{0},
      talker_topic_{}, last_talker_time_ms_{0},
      rx_chatter_topic_{} // 添加这个初始化
```

在`void MiracDDS::on_topic(uxrSession *uxr_session, uxrObjectId object_id, uint16_t request_id, uxrStreamId stream_id, struct ucdrBuffer *ub, uint16_t length)`方法中添加内容

```c
case topics[to_underlying(TopicIndex::TEST_SUB)].data_entity_id.id:
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
```