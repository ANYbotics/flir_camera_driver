#!/usr/bin/env python3

# System imports
import os
from datetime import datetime
import time
import sys
import logging

# ROS imports
import rospy
import rosnode
from sensor_msgs.msg import Image
from anymal_longterm_tests import TopicTimeoutTester
from anymal_longterm_tests import TestNode


# Default timeout If the nominal rate is not found on the param server
TOPIC_TIMEOUT = 0.1
TOPIC_HARD_TIMEOUT = 30
TEST_NAME = "wide_angle_camera_test"

NUM_NOISE_TEST_POINTS = 100


class WacTester(TopicTimeoutTester):
    """
    Wide angle camera test class that inherits from the topic timeout tester. 
    The class tests if the wide angle camera topic times out. It keeps track
    of the number of messages and timeouts and logs the timeout length.

    Args:
        TopicTimeoutTester (TopicTimeoutTester): Base class
    """
    def __init__(self, suffix="front", timeout=TOPIC_TIMEOUT):
        """[summary]

        Args:
            suffix (str, optional): WAC suffix. Defaults to "front".
        """
        super().__init__(topic="/wide_angle_camera_" + suffix +
                               "/image_color",
                         message_type=Image,
                         timeout=timeout, 
                         hard_timeout=TOPIC_HARD_TIMEOUT,
                         custom_rx_hook=self.__check_data_in_bounds)
        self.name = "wide_angle_camera_test_" + suffix
        self.image_prev = None

    def __check_data_in_bounds(self, d):
        # Check if we can see noise on the image.
        # Sample the first couple of pixels and check that they
        # are not exactly the same
        test_points = d.data[:NUM_NOISE_TEST_POINTS]
        if self.image_prev is not None:
            # Check if the first NUM_NOISE_TEST_POINTS points are not 
            # the same as in image_prev
            if self.image_prev == test_points:
                self.data_out_of_bounds += 1
                rospy.logerr("No noise detected on {}".format(self.topic))
        self.image_prev = test_points

    def get_name(self):
        """
        Name of the tester instance

        Returns:
            str: name
        """
        return self.name


def get_enabled_cameras():
    """
    Get the enabled cameras from the parameter server
    and append them to the list of cameras
    """
    cams = []
    if rospy.get_param('/wide_angle_camera_test/front_enabled', True):
        cams.append(WacTester("front"))
    if rospy.get_param('/wide_angle_camera_test/rear_enabled', True):
        cams.append(WacTester("rear"))
    return cams


def main():
    # Run the test and create a log when done
    rospy.init_node(TEST_NAME)
    camera_testers = get_enabled_cameras()
    test = TestNode(TEST_NAME, camera_testers)
    test.run_test()


if __name__ == '__main__':
    main()
