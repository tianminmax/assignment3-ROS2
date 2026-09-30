"""Start the HIKROBOT camera driver, optionally together with RViz2."""

from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    share_dir = Path(get_package_share_directory('hikrobot_camera'))
    default_params = str(share_dir / 'config' / 'camera.yaml')
    default_rviz_config = str(share_dir / 'config' / 'camera.rviz')

    return LaunchDescription([
        DeclareLaunchArgument(
            'params_file',
            default_value=default_params,
            description='Absolute path to the ROS parameter YAML file.',
        ),
        DeclareLaunchArgument(
            'rviz',
            default_value='false',
            description='Also start RViz2 with an Image display for /image_raw.',
        ),
        DeclareLaunchArgument(
            'rviz_config',
            default_value=default_rviz_config,
            description='RViz2 configuration used when rviz:=true.',
        ),
        Node(
            package='hikrobot_camera',
            executable='camera_node',
            name='hikrobot_camera',
            output='screen',
            parameters=[LaunchConfiguration('params_file')],
        ),
        Node(
            package='rviz2',
            executable='rviz2',
            name='rviz2',
            output='screen',
            arguments=['-d', LaunchConfiguration('rviz_config')],
            condition=IfCondition(LaunchConfiguration('rviz')),
        ),
    ])
