# 导入rclpy库，这是ROS 2的Python客户端库
import rclpy
# 导入Node类，我们的节点将继承自这个类
from rclpy.node import Node
# 导入我们将要发布的消息类型String
from std_msgs.msg import String

class MinimalPublisher(Node):

    def __init__(self):
        # 调用父类的构造函数，并给节点命名为'minimal_publisher'
        super().__init__('minimal_publisher')

        # 创建一个发布者。它将发布String类型的消息到名为'talker_topic'的话题上
        # 队列大小(queue size)为10，这是服务质量(QoS)的一个基本设置
        self.publisher_ = self.create_publisher(String, 'talker_topic', 10)

        # 创建一个定时器，每隔0.5秒调用一次timer_callback函数
        timer_period = 0.5  # seconds
        self.timer = self.create_timer(timer_period, self.timer_callback)

        # 初始化一个计数器
        self.i = 0

    def timer_callback(self):
        # 创建一个String类型的消息对象
        msg = String()
        # 填充消息内容
        msg.data = 'Hello World: %d' % self.i

        # 发布消息
        self.publisher_.publish(msg)

        # 在控制台打印日志，确认消息已发出
        self.get_logger().info('Publishing: "%s"' % msg.data)

        # 计数器自增
        self.i += 1

def main(args=None):
    # 初始化rclpy库
    rclpy.init(args=args)

    # 创建我们的发布者节点
    minimal_publisher = MinimalPublisher()

    # rclpy.spin()会保持节点的运行，并处理所有回调（比如定时器回调）
    # 直到程序被中断（例如按下Ctrl+C）
    try:
        rclpy.spin(minimal_publisher)
    except KeyboardInterrupt:
        pass
    finally:
        minimal_publisher.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()

if __name__ == '__main__':
    main()