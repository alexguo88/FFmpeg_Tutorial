#include "CoderMuxer.h"
#include "EncodingMuxingVideo.h"
#include "EncodingMuxingAudio.h"
#include "Stream.h"


/*************************************************
	Function:		hello
	Description:	解析命令行传入的参数
	Calls:			无
	Called By:		main
	Input:			(in)argc : 默认命令行参数
					(in)argv : 默认命令行参数					
	Output:			(out)io_param : 解析命令行的结果
	Return:			true : 命令行解析正确
					false : 命令行解析错误
*************************************************/
static bool hello(int argc, char **argv, AVDictionary *opt, IOParam &ioParam)
{
	if (argc < 5) 
	{
		printf("usage: %s output_file input_file frame_width frame_height\n"
			"API example program to output a media file with libavformat.\n"
			"This program generates a synthetic audio and video stream, encodes and\n"
			"muxes them into a file named output_file.\n"
			"The output format is automatically guessed according to the file extension.\n"
			"Raw images can also be output by using '%%d' in the filename.\n"
			"\n", argv[0]);
		return false;
	}

	ioParam.output_file_name = argv[1];
	ioParam.input_file_name = argv[2];
	ioParam.frame_width = atoi(argv[3]);
	ioParam.frame_height = atoi(argv[4]);

	return true;
}

/*************************************************
Function:		main
Description:	入口点函数
*************************************************/
int main(int argc, char **argv)
{
	AVDictionary *opt = NULL;
	IOParam io = {NULL};
	if (!hello(argc, argv, opt, io))
	{
		return 1;
	}

	int ret;
	int have_video = 0, have_audio = 0;
	int videoFrameIdx = 0, audioFrameIdx = 0;
	OutputStream video_st = { 0 }, audio_st = { 0 };

	int encode_video = 1, encode_audio = 1;
	int64_t videoNextPts = 0, audioNextPts = 0;
	AVOutputFormat *fmt;
	AVFormatContext *oc;
	AVCodec *audio_codec = NULL, *video_codec = NULL;
	AVStream *audioStream = NULL, *videoStream = NULL;
	AVFrame *videoFrame = NULL, *audioFrame = NULL;

	Open_coder_muxer(&fmt, &oc, io.output_file_name);

	//添加视频流
	ret = Add_video_stream(&videoStream, oc, &video_codec, fmt->video_codec);
	if (!ret)
	{
		printf("Adding video stream failed.\n");
		return -1;
	}
	VideoEncodingParam encodingParam = {io.frame_width, io.frame_height, 400000, {1, STREAM_FRAME_RATE}, 12, 2, 2};
	Set_video_stream(&videoStream, encodingParam);

	//添加音频流
	ret = Add_Audio_stream(&audioStream, oc, &audio_codec, fmt->audio_codec);
	if (!ret)
	{
		printf("Adding audio stream failed.\n");
		return -1;
	}
	Set_audio_stream(&audioStream, audio_codec);

	//打开视频流
	if (!Open_video_stream(&videoStream, &videoFrame, video_codec, io))
	{
		printf("Opening video stream failed.\n");
		return -1;
	}

	//打开音频流
	if (!Open_audio_stream(&audioStream, &audioFrame, audio_codec, io))
	{
		printf("Opening audio stream failed.\n");
		return -1;
	}

	av_dump_format(oc, 0, io.output_file_name, 1);

	//打开输出文件
	if (!(fmt->flags & AVFMT_NOFILE))
	{
		ret = avio_open(&oc->pb, io.output_file_name, AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			printf("Could not open '%s': %d\n", io.output_file_name, ret);
			return 1;
		}
	}

	/* Write the stream header, if any. */
	ret = avformat_write_header(oc, NULL);
	if (ret < 0)
	{
		fprintf(stderr, "Error occurred when opening output file: %d\n",ret);
		return 1;
	}

	//写入音频和视频帧
	while (encode_video/* || encode_audio*/) 
	{
		if (encode_video && (!encode_audio || av_compare_ts(videoNextPts, videoStream->time_base, audioNextPts, audioStream->time_base) <= 0))
		{
			encode_video = !Encode_video_frame(oc, &videoStream, &videoFrame, videoNextPts);
			if (encode_video)
			{
				printf("Encode video frame #%d\n", videoFrameIdx++);
			}
			else
			{
				printf("Video ended, exit.\n");
			}
		}
// 		else
// 		{
// 			encode_audio = !Encode_audio_frame(oc, &audioStream, &audioFrame, audioNextPts);
// 			if (encode_audio)
// 			{
// 				printf("Write %d audio frame.\n", audioFrameIdx++);
// 			}
// 			else
// 			{
// 				printf("Audio ended, exit.\n");
// 			}
// 		}
	}

	//写入文件尾结构
	av_write_trailer(oc);

	//关闭音视频流、输出文件、其他收尾工作
	Close_video_stream(&videoStream, &videoFrame);
	Close_audio_stream(&audioStream, &audioFrame);
	if (!(fmt->flags & AVFMT_NOFILE))
	{
		avio_closep(&oc->pb);
	}
	avformat_free_context(oc);

	printf("Procssing succeeded.\n");
	return 0;

	//Discard the belowing:
	while (encode_video || encode_audio) 
	{
		/* select the stream to encode */
		if (encode_video && (!encode_audio || av_compare_ts(video_st.next_pts, video_st.st->codec->time_base, audio_st.next_pts, audio_st.st->codec->time_base) <= 0))
		{
			encode_video = !Write_video_frame(oc, &video_st);
			if (encode_video)
			{
				printf("Write %d video frame.\n", videoFrameIdx++);
			}
			else
			{
				printf("Video ended, exit.\n");
			}
		}
		else 
		{
			encode_audio = !Write_audio_frame(oc, &audio_st);
			if (encode_audio)
			{
				printf("Write %d audio frame.\n", audioFrameIdx++);
			}
			else
			{
				printf("Audio ended, exit.\n");
			}
		}
	}

	/* Write the trailer, if any. The trailer must be written before you
	* close the CodecContexts open when you wrote the header; otherwise
	* av_write_trailer() may try to use memory that was freed on
	* av_codec_close(). */
	av_write_trailer(oc);

	/* Close each codec. */
	if (have_video)
	{
		Close_stream(oc, &video_st);
	}	
	if (have_audio)
	{
		Close_stream(oc, &audio_st);
	}

	if (!(fmt->flags & AVFMT_NOFILE))
		/* Close the output file. */
		avio_closep(&oc->pb);

	/* free the stream */
	avformat_free_context(oc);

	printf("Procssing succeeded.\n");
	return 0;
}