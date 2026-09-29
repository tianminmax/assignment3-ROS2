#include <functional>
#include <memory>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

// 使用 std::placeholders 来绑定成员函数作为回调
using std::placeholders::_1;

// 定义一个继承自 rclcpp::Node 的类
class MinimalSubscriber : public rclcpp::Node
{
public:
    // 构造函数
    MinimalSubscriber()
        : Node("minimal_subscriber") // 节点名称为 "minimal_subscriber"
    {
        // 创建一个订阅者，订阅 "talker_topic" 话题
        // 消息类型为 std_msgs::msg::String
        // 队列大小(QoS)为 10
        // 回调函数为 topic_callback
        subscription_ = this->create_subscription<std_msgs::msg::String>(
                            "talker_topic", 10, std::bind(&MinimalSubscriber::topic_callback, this, _1));
    }

private:
    // 收到消息时被调用的回调函数
    void topic_callback(const std_msgs::msg::String & msg) const
    {
        // 使用日志宏打印接收到的消息
        RCLCPP_INFO(this->get_logger(), "I heard: '%s'", msg.data.c_str());
    }

    // 声明订阅者指针
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
};

int main(int argc, char * argv[])
{
    // 初始化 ROS 2 C++ 客户端库
    rclcpp::init(argc, argv);
    // 创建 MinimalSubscriber 节点并开始自旋 (spin)，等待消息
    rclcpp::spin(std::make_shared<MinimalSubscriber>());
    // 关闭 ROS 2
    rclcpp::shutdown();
    return 0;
}