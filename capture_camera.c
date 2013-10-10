/*******************************************************************************
# capture_camera: USB UVC Video Class Snapshot Software	                       #
#                                                                              #
#   Copyright (C) 2013 R. Pérez-Pérez                                          #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; either version 2 of the License, or            #
# (at your option) any later version.                                          #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
*******************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <tiffio.h>

#include <getopt.h>		/* getopt_long() */

#define MAX_EXPOSURE_TIME 10000

enum v4l2_uvc_exposure_auto_type {
	V4L2_UVC_EXPOSURE_MANUAL = 1,
	V4L2_UVC_EXPOSURE_AUTO = 2,
	V4L2_UVC_EXPOSURE_SHUTTER_PRIORITY = 4,
	V4L2_UVC_EXPOSURE_APERTURE_PRIORITY = 8
};

uint8_t *buffer;
static char *dev_name = NULL;
static int exposure_time = -1;
static int sw = 1280;
static int sh = 960;

struct v4l2_queryctrl queryctrl;
struct v4l2_querymenu querymenu;
struct v4l2_control control;

static void yuy2tiff(char *outputfile)
{

	unsigned char *yuyv;
	int z;
	int sampleperpixel = 3;

	char cadena[255];
	sprintf(cadena, "%s.tif", outputfile);

	TIFF *out = TIFFOpen(cadena, "w");

	TIFFSetField(out, TIFFTAG_IMAGEWIDTH, sw);	// set the width of the image
	TIFFSetField(out, TIFFTAG_IMAGELENGTH, sh);	// set the height of the image
	TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, sampleperpixel);	// set number of channels per pixel
	TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);	// set the size of the channels
	TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);	// set the origin of the image.
//   Some other essential fields to set that you do not have to understand for now.
	TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);

	fprintf(stderr, "Convert YUYV frame to TIFF image.\n");

	tsize_t linebytes = sampleperpixel * sw;
	unsigned char *buf = NULL;	// buffer used to store the row of pixel information for writing to file
//    Allocating memory to store the pixels of current row
	if (TIFFScanlineSize(out) == linebytes)
		buf = (unsigned char *) _TIFFmalloc(linebytes);
	else
		buf = (unsigned char *) _TIFFmalloc(TIFFScanlineSize(out));

// We set the strip size of the file to be size of one row of pixels
	TIFFSetField(out, TIFFTAG_ROWSPERSTRIP,
		     TIFFDefaultStripSize(out, sw * sampleperpixel));

	yuyv = buffer;

	z = 0;
	uint32 row = 0;
	for (; row < sh; row++) {

		int x;
		unsigned char *ptr = buf;

		for (x = 0; x < sw; x++) {
			int r, g, b;
			int y, u, v;

			if (!z)
				y = yuyv[0] << 8;
			else
				y = yuyv[2] << 8;
			u = yuyv[1] - 128;
			v = yuyv[3] - 128;

			r = (y + (359 * v)) >> 8;
			g = (y - (88 * u) - (183 * v)) >> 8;
			b = (y + (454 * u)) >> 8;

			*(ptr++) = (r > 255) ? 255 : ((r < 0) ? 0 : r);
			*(ptr++) = (g > 255) ? 255 : ((g < 0) ? 0 : g);
			*(ptr++) = (b > 255) ? 255 : ((b < 0) ? 0 : b);

			if (z++) {
				z = 0;
				yuyv += 4;
			}
		}

		if (TIFFWriteScanline(out, buf, row, 0) < 0) {
			printf("Error TIFF\n");
			break;
		}
	}

	TIFFClose(out);
	if (buf)
		_TIFFfree(buf);

	printf("Save %s file\n", cadena);

}

static int xioctl(int fd, int request, void *arg)
{
	int r;

	do
		r = ioctl(fd, request, arg);
	while (-1 == r && EINTR == errno);

	return r;
}

int print_caps(int fd)
{
	struct v4l2_capability caps = { };
	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &caps)) {
		perror("Querying Capabilities");
		return 1;
	}

	printf("Driver Caps:\n"
	       "  Driver: \"%s\"\n"
	       "  Card: \"%s\"\n"
	       "  Bus: \"%s\"\n"
	       "  Version: %d.%d\n"
	       "  Capabilities: %08x\n",
	       caps.driver,
	       caps.card,
	       caps.bus_info,
	       (caps.version >> 16) && 0xff,
	       (caps.version >> 24) && 0xff, caps.capabilities);


	struct v4l2_cropcap cropcap = { 0 };
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
		perror("Querying Cropping Capabilities");
		return 1;
	}

	printf("Camera Cropping:\n"
	       "  Bounds: %dx%d+%d+%d\n"
	       "  Default: %dx%d+%d+%d\n"
	       "  Aspect: %d/%d\n",
	       cropcap.bounds.width, cropcap.bounds.height,
	       cropcap.bounds.left, cropcap.bounds.top,
	       cropcap.defrect.width, cropcap.defrect.height,
	       cropcap.defrect.left, cropcap.defrect.top,
	       cropcap.pixelaspect.numerator,
	       cropcap.pixelaspect.denominator);

	int support_grbg10 = 0;

	struct v4l2_fmtdesc fmtdesc = { 0 };
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	char fourcc[5] = { 0 };
	char c, e;
	printf("  FMT : CE Desc\n--------------------\n");
	while (0 == xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc)) {
		strncpy(fourcc, (char *) &fmtdesc.pixelformat, 4);
		if (fmtdesc.pixelformat == V4L2_PIX_FMT_SGRBG10)
			support_grbg10 = 1;
		c = fmtdesc.flags & 1 ? 'C' : ' ';
		e = fmtdesc.flags & 2 ? 'E' : ' ';
		printf("  %s: %c%c %s\n", fourcc, c, e,
		       fmtdesc.description);
		fmtdesc.index++;
	}

	if (!support_grbg10) {
		printf("Doesn't support GRBG10.\n");
		// return 1;
	}

	struct v4l2_format fmt = { 0 };
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = sw;
	fmt.fmt.pix.height = sh;
//      fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SGRBG10;
//      fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;	// format 422
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt)) {
		perror("Setting Pixel Format");
		return 1;
	}

	strncpy(fourcc, (char *) &fmt.fmt.pix.pixelformat, 4);
	printf("Selected Camera Mode:\n"
	       "  Width: %d\n"
	       "  Height: %d\n"
	       "  PixFmt: %s\n"
	       "  Field: %d\n",
	       fmt.fmt.pix.width,
	       fmt.fmt.pix.height, fourcc, fmt.fmt.pix.field);
	return 0;
}

int init_mmap(int fd)
{
	struct v4l2_requestbuffers req = { 0 };
	req.count = 1;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		perror("Requesting Buffer");
		return 1;
	}

	struct v4l2_buffer buf = { 0 };
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;
	if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf)) {
		perror("Querying Buffer");
		return 1;
	}

	buffer =
	    mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		 buf.m.offset);
	printf("Length: %d\n", buf.length);

	return 0;
}

int capture_image(int fd, char *outputfile)
{
	struct v4l2_buffer buf = { 0 };
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;
	if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) {
		perror("Query Buffer");
		return 1;
	}

	if (-1 == xioctl(fd, VIDIOC_STREAMON, &buf.type)) {
		perror("Start Capture");
		return 1;
	}

	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	struct timeval tv = { 0 };
	tv.tv_sec = 2;
	int r = select(fd + 1, &fds, NULL, NULL, &tv);
	if (-1 == r) {
		perror("Waiting for Frame");
		return 1;
	}

	if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
		perror("Retrieving Frame");
		return 1;
	}

	int ret;
	int f = open(outputfile, O_CREAT | O_TRUNC | O_RDWR, 0660);
	int len = buf.bytesused;
	while (len > 0) {
		ret = write(f, buffer, len);
		if (ret < 0)
			break;
		len -= ret;
	}
	printf("Dumped %d bytes into %s (YUV-file 422 format)\n",
	       buf.bytesused, outputfile);
	close(f);
	yuy2tiff(outputfile);

	return 0;
}

static void usage(FILE * fp, int argc, char **argv)
{
	fprintf(fp, "Usage: %s [options]\n\n" "Options:\n" "-d | --device name   Video device name [/dev/video0]\n" "-e | --exposure_time Exposure Time (optional to auto exposure time)\n" "-h | --help          Print this message\n" "-x | --sw            Still image width\n" "-y | --sh            Still image height\n" "-o | --output        Output filename (default: snap.yuv).\n"	//Use img%%05d.yuv for sequential files\n"
		"", argv[0]);
}

static const char short_options[] = "d:e:x:y:o:";

static const struct option long_options[] = {
	{"device", required_argument, NULL, 'd'},
	{"exposure", required_argument, NULL, 'e'},
	{"sw", required_argument, NULL, 'x'},
	{"sh", required_argument, NULL, 'y'},
	{"output", required_argument, NULL, 'o'},
	{0, 0, 0, 0}
};


int main(int argc, char *argv[])
{

	dev_name = "/dev/video0";
	int value;
	char *outputfile = "snap.yuv";

	for (;;) {
		int index;
		int c;

		c = getopt_long(argc, argv,
				short_options, long_options, &index);

		if (-1 == c)
			break;

		switch (c) {
		case 0:	/* getopt_long() flag */
			break;

		case 'd':
			dev_name = optarg;
			break;

		case 'o':
			outputfile = optarg;
			break;

		case 'e':
			value = atoi(optarg);
			exposure_time =
			    value > 0 ? value : MAX_EXPOSURE_TIME;
			break;

		case 'h':
			usage(stdout, argc, argv);
			exit(EXIT_SUCCESS);

		case 'x':
			value = atoi(optarg);
			if (value > 0) {
				sw = value;
			}
			break;

		case 'y':
			value = atoi(optarg);
			if (value > 0) {
				sh = value;
			}
			break;

		default:
			usage(stderr, argc, argv);
			exit(EXIT_FAILURE);
		}
	}

	int fd;
	fd = open(dev_name, O_RDWR);
	if (fd == -1) {
		perror("Opening video device");
		return 1;
	}

	if (print_caps(fd))
		return 1;

	if (init_mmap(fd))
		return 1;

	// show default values in exposure time
	printf("NOTE: v4l2-ctl -L to query parameters\n");

	if (exposure_time > -1) {
		memset(&queryctrl, 0, sizeof(queryctrl));
		queryctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;

		if (-1 == xioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			if (errno != EINVAL) {
				perror("VIDIOC_QUERYCTRL");
				exit(EXIT_FAILURE);
			} else {
				printf
				    ("V4L2_CID_EXPOSURE_ABSOLUTE is not supported\n");
			}
		} else if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
			printf("V4L2_CID is not supported\n");
		} else {

			// set manual flags to modify the exposure time
			memset(&queryctrl, 0, sizeof(queryctrl));
			queryctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
			queryctrl.flags &= !(V4L2_CTRL_FLAG_GRABBED);	// On exposure absolute
			if (-1 == xioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
				if (errno != EINVAL) {
					perror("VIDIOC_QUERYCTRL");
					exit(EXIT_FAILURE);
				} else {
					printf
					    ("V4L2_CID_EXPOSURE_ABSOLUTE is not supported\n");
				}
			}
			// set class camera control
			struct v4l2_ext_controls query = { 0 };
			struct v4l2_ext_control ctrl[1];
			query.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
			ctrl[0].id = V4L2_CID_EXPOSURE_AUTO;
			ctrl[0].value = V4L2_EXPOSURE_MANUAL;

			query.count = 1;
			query.controls = ctrl;

			if (-1 ==
			    ioctl(fd, VIDIOC_S_EXT_CTRLS, &query,
				  "VIDIOC_S_EXT_CTRLS")) {
				perror("VIDIOC_G_CTRL get camera class");
				exit(EXIT_FAILURE);
			} else {
				printf("Set V4L2_CTRL_CLASS_CAMERA OK\n");
			}

			//change the exposure time
			memset(&control, 0, sizeof(control));
			control.id = V4L2_CID_EXPOSURE_ABSOLUTE;
			control.value = exposure_time;	//queryctrl.default_value;//10000;//exposure_time;

			if (-1 == xioctl(fd, VIDIOC_S_CTRL, &control)) {
				perror("VIDIOC_S_CTRL set exposure time");
				exit(EXIT_FAILURE);
			}
			printf("Set Exposicion time OK\n");
		}

	} else {

		// set manual flags to set exposure auto
		memset(&queryctrl, 0, sizeof(queryctrl));
		queryctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
		queryctrl.flags |= V4L2_CTRL_FLAG_GRABBED;
		if (-1 == xioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			if (errno != EINVAL) {
				perror("VIDIOC_QUERYCTRL");
				exit(EXIT_FAILURE);
			} else {
				printf
				    ("V4L2_CID_EXPOSURE_ABSOLUTE is not supported\n");
			}
		}


		struct v4l2_ext_controls query = { 0 };
		struct v4l2_ext_control ctrl[1];
		query.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
		ctrl[0].id = V4L2_CID_EXPOSURE_AUTO;
		ctrl[0].value = V4L2_EXPOSURE_APERTURE_PRIORITY;

		query.count = 1;
		query.controls = ctrl;

		if (-1 ==
		    ioctl(fd, VIDIOC_S_EXT_CTRLS, &query,
			  "VIDIOC_S_EXT_CTRLS")) {
			perror("VIDIOC_G_CTRL get camera class");
			exit(EXIT_FAILURE);
		} else {
			printf("Set V4L2_CTRL_CLASS_CAMERA OK\n");
		}


	}

	if (capture_image(fd, outputfile))
		return 1;

	close(fd);
	return 0;
}
