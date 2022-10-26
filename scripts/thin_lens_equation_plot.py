#!/usr/bin/env python3
"""
Plot thin lens equation of a camera.
THIN LENS: A thin lens is defined to be one whose thickness allows rays to refract but does not
allow properties such as dispersion and aberrations.

THIN LENS EQUATION: 1/d_{o} + 1/d_{i} = 1/f where d_{o} is the object distance, d_{i} is the image distance, f is the focal length of the
camera lens. See /resources/image_formulation.jpg
"""
import argparse

import matplotlib.pyplot as plt
import numpy as np


def plot_thin_lens_equation(focal_length: float) -> None:
    """
    Plot thin lens equation
    :param focal_length: The focal length of the lens. In millimeter.
    :return: Plot
    """
    # Object distance is in meter.
    object_distance = np.linspace(focal_length + 0.01, 100, 100)
    # Image distance is in mm.
    image_distance = 1 / (1 / focal_length - 1 / object_distance)*1000

    # plot
    fig, ax = plt.subplots()
    ax.plot(object_distance, image_distance, linewidth=2.0)
    ax.set(xlabel='object distance d_{o} (m)', ylabel='image distance d_{i} (mm)',
           title=f'Object and image distances relationship in thin lens equation with a focal length of {focal_length*1000}mm.')
    ax.grid()
    plt.show()


def main():
    """
    Entry of plotting thin lens equation
    :return:
    """
    parser = argparse.ArgumentParser(description="Plot thin lens equation.",
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument("-f", "--focal-length", default=1.93, type=float,
                        help="The focal length of the lens. Unit: millimeter")

    args = parser.parse_args()

    if args.focal_length == 0:
        print("The focal length should not be zero.")
        return

    plot_thin_lens_equation(args.focal_length / 1000)


if __name__ == "__main__":
    main()
