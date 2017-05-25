/*
Author:Jack-Cui
Blog:http://blog.csdn.net/c406495762
Time:25 May 2017
*/
#include <unistd.h>
#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <pthread.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <iomanip>
#include <string>

using namespace std;

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define IMAGEWIDTH 3264
#define IMAGEHEIGHT 2448

class V4L2Capture {
public:
	V4L2Capture(char *devName, int width, int height);
	virtual ~V4L2Capture();

	int openDevice();
	int closeDevice();
	int initDevice();
	int startCapture();
	int stopCapture();
	int freeBuffers();
	int getFrame(void **,size_t *);
	int backFrame();

	static void test();

private:
	int initBuffers();

	struct cam_buffer
	{
		void* start;
		unsigned int length;
	};
	char *devName;
	int capW;
	int capH;
	int fd_cam;
	cam_buffer *buffers;
	unsigned int n_buffers;
	int frameIndex;
};

V4L2Capture::V4L2Capture(char *devName, int width, int height) {
	// TODO Auto-generated constructor stub
	this->devName = devName;
	this->fd_cam = -1;
	this->buffers = NULL;
	this->n_buffers = 0;
	this->frameIndex = -1;
	this->capW=width;
	this->capH=height;
}

V4L2Capture::~V4L2Capture() {
	// TODO Auto-generated destructor stub
}

int V4L2Capture::openDevice() {
	/*设备的打开*/
	printf("video dev : %s\n", devName);
	fd_cam = open(devName, O_RDWR);
	if (fd_cam < 0) {
		perror("Can't open video device");
	}
	return 0;
}

int V4L2Capture::closeDevice() {
	if (fd_cam > 0) {
		int ret = 0;
		if ((ret = close(fd_cam)) < 0) {
			perror("Can't close video device");
		}
		return 0;
	} else {
		return -1;
	}
}

int V4L2Capture::initDevice() {
	int ret;
	struct v4l2_capability cam_cap;		//显示设备信息
	struct v4l2_cropcap cam_cropcap;	//设置摄像头的捕捉能力
	struct v4l2_fmtdesc cam_fmtdesc;	//查询所有支持的格式：VIDIOC_ENUM_FMT
	struct v4l2_crop cam_crop;			//图像的缩放
	struct v4l2_format cam_format;		//设置摄像头的视频制式、帧格式等

	/* 使用IOCTL命令VIDIOC_QUERYCAP，获取摄像头的基本信息*/
	ret = ioctl(fd_cam, VIDIOC_QUERYCAP, &cam_cap);
	if (ret < 0) {
		perror("Can't get device information: VIDIOCGCAP");
	}
	printf(
			"Driver Name:%s\nCard Name:%s\nBus info:%s\nDriver Version:%u.%u.%u\n",
			cam_cap.driver, cam_cap.card, cam_cap.bus_info,
			(cam_cap.version >> 16) & 0XFF, (cam_cap.version >> 8) & 0XFF,
			cam_cap.version & 0XFF);

	/* 使用IOCTL命令VIDIOC_ENUM_FMT，获取摄像头所有支持的格式*/
	cam_fmtdesc.index = 0;
	cam_fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	printf("Support format:\n");
	while (ioctl(fd_cam, VIDIOC_ENUM_FMT, &cam_fmtdesc) != -1) {
		printf("\t%d.%s\n", cam_fmtdesc.index + 1, cam_fmtdesc.description);
		cam_fmtdesc.index++;
	}

	/* 使用IOCTL命令VIDIOC_CROPCAP，获取摄像头的捕捉能力*/
	cam_cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (0 == ioctl(fd_cam, VIDIOC_CROPCAP, &cam_cropcap)) {
		printf("Default rec:\n\tleft:%d\n\ttop:%d\n\twidth:%d\n\theight:%d\n",
				cam_cropcap.defrect.left, cam_cropcap.defrect.top,
				cam_cropcap.defrect.width, cam_cropcap.defrect.height);
		/* 使用IOCTL命令VIDIOC_S_CROP，获取摄像头的窗口取景参数*/
		cam_crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cam_crop.c = cam_cropcap.defrect;		//默认取景窗口大小
		if (-1 == ioctl(fd_cam, VIDIOC_S_CROP, &cam_crop)) {
			//printf("Can't set crop para\n");
		}
	} else {
		printf("Can't set cropcap para\n");
	}

	/* 使用IOCTL命令VIDIOC_S_FMT，设置摄像头帧信息*/
	cam_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	cam_format.fmt.pix.width = capW;
	cam_format.fmt.pix.height = capH;
	cam_format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;		//要和摄像头支持的类型对应
	cam_format.fmt.pix.field = V4L2_FIELD_INTERLACED;
	ret = ioctl(fd_cam, VIDIOC_S_FMT, &cam_format);
	if (ret < 0) {
		perror("Can't set frame information");
	}
	/* 使用IOCTL命令VIDIOC_G_FMT，获取摄像头帧信息*/
	cam_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd_cam, VIDIOC_G_FMT, &cam_format);
	if (ret < 0) {
		perror("Can't get frame information");
	}
	printf("Current data format information:\n\twidth:%d\n\theight:%d\n",
			cam_format.fmt.pix.width, cam_format.fmt.pix.height);
	ret = initBuffers();
	if (ret < 0) {
		perror("Buffers init error");
		//exit(-1);
	}
	return 0;
}

int V4L2Capture::initBuffers() {
	int ret;
	/* 使用IOCTL命令VIDIOC_REQBUFS，申请帧缓冲*/
	struct v4l2_requestbuffers req;
	CLEAR(req);
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd_cam, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		perror("Request frame buffers failed");
	}
	if (req.count < 2) {
		perror("Request frame buffers while insufficient buffer memory");
	}
	buffers = (struct cam_buffer*) calloc(req.count, sizeof(*buffers));
	if (!buffers) {
		perror("Out of memory");
	}
	for (n_buffers = 0; n_buffers < req.count; n_buffers++) {
		struct v4l2_buffer buf;
		CLEAR(buf);
		// 查询序号为n_buffers 的缓冲区，得到其起始物理地址和大小
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;
		ret = ioctl(fd_cam, VIDIOC_QUERYBUF, &buf);
		if (ret < 0) {
			printf("VIDIOC_QUERYBUF %d failed\n", n_buffers);
			return -1;
		}
		buffers[n_buffers].length = buf.length;
		//printf("buf.length= %d\n",buf.length);
		// 映射内存
		buffers[n_buffers].start = mmap(
				NULL, // start anywhere
				buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_cam,
				buf.m.offset);
		if (MAP_FAILED == buffers[n_buffers].start) {
			printf("mmap buffer%d failed\n", n_buffers);
			return -1;
		}
	}
	return 0;
}

int V4L2Capture::startCapture() {
	unsigned int i;
	for (i = 0; i < n_buffers; i++) {
		struct v4l2_buffer buf;
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (-1 == ioctl(fd_cam, VIDIOC_QBUF, &buf)) {
			printf("VIDIOC_QBUF buffer%d failed\n", i);
			return -1;
		}
	}
	enum v4l2_buf_type type;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == ioctl(fd_cam, VIDIOC_STREAMON, &type)) {
		printf("VIDIOC_STREAMON error");
		return -1;
	}
	return 0;
}

int V4L2Capture::stopCapture() {
	enum v4l2_buf_type type;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == ioctl(fd_cam, VIDIOC_STREAMOFF, &type)) {
		printf("VIDIOC_STREAMOFF error\n");
		return -1;
	}
	return 0;
}

int V4L2Capture::freeBuffers() {
	unsigned int i;
	for (i = 0; i < n_buffers; ++i) {
		if (-1 == munmap(buffers[i].start, buffers[i].length)) {
			printf("munmap buffer%d failed\n", i);
			return -1;
		}
	}
	free(buffers);
	return 0;
}

int V4L2Capture::getFrame(void **frame_buf, size_t* len) {
	struct v4l2_buffer queue_buf;
	CLEAR(queue_buf);
	queue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue_buf.memory = V4L2_MEMORY_MMAP;
	if (-1 == ioctl(fd_cam, VIDIOC_DQBUF, &queue_buf)) {
		printf("VIDIOC_DQBUF error\n");
		return -1;
	}
	*frame_buf = buffers[queue_buf.index].start;
	*len = buffers[queue_buf.index].length;
	frameIndex = queue_buf.index;
	return 0;
}

int V4L2Capture::backFrame() {
	if (frameIndex != -1) {
		struct v4l2_buffer queue_buf;
		CLEAR(queue_buf);
		queue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		queue_buf.memory = V4L2_MEMORY_MMAP;
		queue_buf.index = frameIndex;
		if (-1 == ioctl(fd_cam, VIDIOC_QBUF, &queue_buf)) {
			printf("VIDIOC_QBUF error\n");
			return -1;
		}
		return 0;
	}
	return -1;
}

void V4L2Capture::test() {
	unsigned char *yuv422frame = NULL;
	unsigned long yuvframeSize = 0;

	string videoDev="/dev/video0";
	V4L2Capture *vcap = new V4L2Capture(const_cast<char*>(videoDev.c_str()),
			1920, 1080);
	vcap->openDevice();
	vcap->initDevice();
	vcap->startCapture();
	vcap->getFrame((void **) &yuv422frame, &yuvframeSize);

	vcap->backFrame();
	vcap->freeBuffers();
	vcap->closeDevice();
}

void VideoPlayer() {
	unsigned char *yuv422frame = NULL;
	unsigned long yuvframeSize = 0;

	string videoDev = "/dev/video0";
	V4L2Capture *vcap = new V4L2Capture(const_cast<char*>(videoDev.c_str()), 1920, 1080);
	vcap->openDevice();
	vcap->initDevice();
	vcap->startCapture();

	cvNamedWindow("Capture",CV_WINDOW_AUTOSIZE);
	IplImage* img;
	CvMat cvmat;
	double t;
	while(1){
		t = (double)cvGetTickCount();
		vcap->getFrame((void **) &yuv422frame, &yuvframeSize);
		cvmat = cvMat(IMAGEHEIGHT,IMAGEWIDTH,CV_8UC3,(void*)yuv422frame);		//CV_8UC3

		//解码
		img = cvDecodeImage(&cvmat,1);
		if(!img){
			printf("DecodeImage error!\n");
		}

		cvShowImage("Capture",img);
		cvReleaseImage(&img);

		vcap->backFrame();
		if((cvWaitKey(1)&255) == 27){
			exit(0);
		}
		t = (double)cvGetTickCount() - t;
		printf("Used time is %g ms\n",( t / (cvGetTickFrequency()*1000)));
	}		
	vcap->stopCapture();
	vcap->freeBuffers();
	vcap->closeDevice();

}

int main() {
	VideoPlayer();
	return 0;
}