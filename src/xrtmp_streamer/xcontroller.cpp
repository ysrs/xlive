#include "xcontroller.h"
#include "xvideo_capture.h"
#include "xaudio_record.h"
#include "xmedia_encode.h"
#include "xrtmp.h"
#include <iostream>


using namespace std;
XController::XController()
{
}


XController::~XController()
{
}


XController *XController::Get()
{
	static XController xc;
	return &xc;
}

bool XController::Start()
{
	///1 设置磨皮过滤器
	XVideoCapture::Get()->ADDFilter(XFilter::Get());
	cout << "1 设置磨皮过滤器" << endl;

	///2 打开相机
	if (camIndex >= 0)
	{
		if (!XVideoCapture::Get()->Init(camIndex))
		{
			err = "2 打开索引为 ";
			err += camIndex;
			err += " 的系统相机失败";
			cout << err << endl;
			return false;
		}
	}
	else if (!inUrl.empty())
	{
		if (!XVideoCapture::Get()->Init(inUrl.c_str()))
		{
			err = "2 打开 ";
			err += inUrl;
			err += " 的网络相机失败";
			cout << err << endl;
			return false;
		}
	}
	else
	{
		err = "2 请设置相机参数";
		cout << err << endl;
		return false;
	}
	cout << "2 相机打开成功！" << endl;

	///3 qt的音频开始录制
	if (!XAudioRecord::Get()->Init())
	{
		err = "3 录音设备打开失败";
		cout << err << endl;
		return false;
	}
	cout << "3 录音设备打开成功！" << endl;

	///启动音视频录制线程
	XAudioRecord::Get()->Start();
	XVideoCapture::Get()->Start();

	///4 音视频编码类
	///初始化格式转换上下文
	///初始化格式输出的数据结构
	XMediaEncode::Get()->inWidth = XVideoCapture::Get()->width;
	XMediaEncode::Get()->inHeight = XVideoCapture::Get()->height;
	XMediaEncode::Get()->outWidth = XVideoCapture::Get()->width;
	XMediaEncode::Get()->outHeight = XVideoCapture::Get()->height;
	if (!XMediaEncode::Get()->InitScale())
	{
		err = "4 视频像素格式转换失败！";
		cout << err << endl;
		return false;
	}
	cout << "4 视频像素格式转换成功！" << endl;

	///5 音频重采样上下文初始化
	XMediaEncode::Get()->channels = XAudioRecord::Get()->channels;
	XMediaEncode::Get()->nbSamples = XAudioRecord::Get()->nbSamples;
	XMediaEncode::Get()->sampleRate = XAudioRecord::Get()->sampleRate;
	if (!XMediaEncode::Get()->InitResample())
	{
		err = "5 音频重采样上下文初始化失败！";
		cout << err << endl;
		return false;
	}
	cout << "5 音频重采样上下文初始化成功！" << endl;

	///6 初始化音频编码器
	if (!XMediaEncode::Get()->InitAudioCodec())
	{
		err = "6 音频编码器初始化失败！";
		cout << err << endl;
		return false;
	}
	cout << "6 音频编码器初始化成功！" << endl;

	///7 初始化视频编码器
	if (!XMediaEncode::Get()->InitVideoCodec())
	{
		err = "7 视频编码器初始化失败！";
		cout << err << endl;
		return false;
	}
	cout << "7 视频编码器初始化成功！" << endl;

	///8 创建输出封装器上下文
	if (!XRtmp::Get()->Init(outUrl.c_str()))
	{
		err = "8 创建输出封装器上下文失败！";
		cout << err << endl;
		return false;
	}
	cout << "8 创建输出封装器上下文成功！" << endl;

	///9 添加音视频流
	vIndex = XRtmp::Get()->AddStream(XMediaEncode::Get()->vc);
	aIndex = XRtmp::Get()->AddStream(XMediaEncode::Get()->ac);
	if (vIndex < 0 || aIndex < 0)
	{
		err = "9 添加音视频流失败！";
		cout << err << endl;
		return false;
	}
	cout << "9 添加音视频流成功！" << endl;

	///10 写入分装头
	if (!XRtmp::Get()->SendHead())
	{
		err = "10 发送封装头失败！";
		cout << err << endl;
		return false;
	}
	cout << "10 发送封装头成功！" << endl;

	XAudioRecord::Get()->Clear();
	XVideoCapture::Get()->Clear();
	XDataThread::Start();
	
	return true;
}

void XController::Stop()
{
	XDataThread::Stop();
	XAudioRecord::Get()->Stop();
	XVideoCapture::Get()->Stop();
	XMediaEncode::Get()->Close();
	XRtmp::Get()->Close();

	camIndex = -1;
	inUrl = "";
}

bool XController::Set(std::string key, double val)
{
	return XFilter::Get()->Set(key, val);
}

void XController::run()
{
	long long beginTime = GetCurTime();

	while (!isExit)
	{
		// 一次读取一帧音频
		XData ad = XAudioRecord::Get()->Pop();
		XData vd = XVideoCapture::Get()->Pop();

		if (ad.size <= 0 && vd.size <= 0)
		{
			msleep(1);
			continue;
		}
		// 处理音频
		if (ad.size > 0)
		{
			ad.pts = ad.pts - beginTime;
			// 重采样源数据
			XData pcm = XMediaEncode::Get()->Resample(ad);
			ad.Drop();

			XData pkt = XMediaEncode::Get()->EncodeAudio(pcm);
			if (pkt.size > 0)
			{
				// 推流
				if (XRtmp::Get()->SendFrame(pkt, aIndex))
				{
					cout << "#" << flush;
				}
			}
		}

		// 处理视频
		if (vd.size > 0)
		{
			vd.pts = vd.pts - beginTime;

			XData yuv = XMediaEncode::Get()->RGBToYUV(vd);
			vd.Drop();

			XData pkt = XMediaEncode::Get()->EncodeVideo(yuv);
			if (pkt.size > 0)
			{
				// 推流
				if (XRtmp::Get()->SendFrame(pkt, vIndex))
				{
					cout << "@" << flush;
				}
			}
		}
	}
}
