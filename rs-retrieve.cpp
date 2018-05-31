// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2017 Intel Corporation. All Rights Reserved.

#include <iostream>
#include <vector>
// FFmpeg
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
}
#include <time.h>
#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <opencv2/opencv.hpp>   // Include OpenCV API
#include <opencv2/highgui.hpp>

#define USE_VIDEO_SYNC

/*
// returns 0 if ok, -1 if there was no frame found
// search_size is the max no of bytes to look for
// at 10 mbps, 30fps, each frame will be about 42K byes
*/
static int seek_to_frame(FILE* pFile, int search_size){

    int ret;
    char *ptr;
    // save current file pointer
    int start_pos = ftell(pFile);

    char *buf = (char *) malloc(sizeof(char) * search_size);
    if(buf == NULL)
        return -1;

    char needle[4] = {0x00, 0x00, 0x00, 0x01};
    char *last_needle = NULL;

    int bytes_read =  fread (buf,1,search_size,pFile);
    if (bytes_read > 0)
       ptr = (char *) memmem((void *) buf, bytes_read, needle, 4);

    if (!ptr) return -1;

    int frame_size = ptr - buf;
    fseek( pFile , (start_pos + frame_size) , SEEK_SET );
    return 0;
}

/*
// reads next h264 packet in to buf
// returns frame size or -1 if error
// it is assumed that pFile is at frame boundary
// buf_size is the max size on buf
*/
static int  read_next_frame(FILE* pFile, int buf_size, char *buf){

    // save current file pointer
    int start_pos = ftell(pFile);

    // we want to seek to next frame(), so increment handle beyond current frame boundary
    fseek ( pFile , start_pos+1 , SEEK_SET );
    int ret = seek_to_frame(pFile, 42000);
    if (ret != 0){
        return -1;
    }

    // new_pFile - old_pFile is the frame size
    int end_pos = ftell(pFile);
    int frame_size = end_pos - start_pos;

    // seek to old pFile
    fseek ( pFile , start_pos , SEEK_SET );

    if (frame_size <= buf_size) {
        // read data
        int bytes_read =  fread (buf,1,frame_size,pFile);
        if(bytes_read == frame_size)
            return bytes_read;
        else
            return -1;
    }
    else {
        return -1;
    }
}


int main(int argc, char * argv[]) try
{

    using namespace cv;
    const auto window_name = "Display Image";

#ifdef USE_VIDEO_SYNC
    uint64_t last_frame_display_time = 0;
#endif

    int retVal = EXIT_SUCCESS;
    int ret;
    const char* infile = argv[1];
    FILE* pFile = fopen (infile, "rb");

    AVCodec *vcodec = 0;
    AVCodecContext *pCodecCtx;
    AVFrame* frame;
    AVFrame* decframe;
    AVPacket packet;

    unsigned nb_frames = 0;
    bool end_of_stream = false;
    int got_pic = 0;

    // we need to know width, height & fps
    // as of now hard-coded values
    // but these could from a txt file
    int video_height = 480;
    int video_width = 640;
    int video_fps = 30;

    const int dst_width = video_width;
    const int dst_height = video_height;
    const AVPixelFormat dst_pix_fmt = AV_PIX_FMT_BGR24;


    if (argc < 2) {
        std::cout << "Usage: rs-retrieve <infile>" << std::endl;
        return EXIT_FAILURE;
    }

    if(pFile == NULL) {
        std::cout << "Can not open file for writing" << std::endl;
        return EXIT_FAILURE;
    }

    if (seek_to_frame(pFile, 42000) != 0) {
        std::cout << "Failed to find a valid encoded frame" << std::endl;
        fclose(pFile);
        return EXIT_FAILURE;
    }


    // initialize FFmpeg library
    av_register_all();

    avformat_network_init();


    // image_encoding = enc::RGB8;
    // int video_step = video_width*3;

    // Check codec support
    vcodec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!vcodec)
    {
        std::cout << "Unable to find video encoder";
        fclose(pFile);
        return EXIT_FAILURE;
    }

    pCodecCtx = avcodec_alloc_context3(vcodec);
    if (!pCodecCtx)
    {
        std::cout << "Unable to allocate encoder context";
        fclose(pFile);
        return EXIT_FAILURE;
    }

    pCodecCtx->width = video_width;
    pCodecCtx->height= video_height;
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

    // open the decoder
    ret = avcodec_open2(pCodecCtx, vcodec, NULL);
    if (ret < 0) {
        std::cerr << "fail to avcodec_open2: ret=" << ret;
        fclose(pFile);
        return EXIT_FAILURE;
    }

    // initialize sample scaler

    SwsContext* swsctx = sws_getCachedContext(
        nullptr, pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        dst_width, dst_height, dst_pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!swsctx) {
        avcodec_close(pCodecCtx);
        fclose(pFile);
        return EXIT_FAILURE;
    }
    std::cout << "output: " << dst_width << 'x' << dst_height << ',' << av_get_pix_fmt_name(dst_pix_fmt) << std::endl;

    // allocate frame buffer for output
    frame = av_frame_alloc();
    if(frame ==NULL){
        avcodec_close(pCodecCtx);
        fclose(pFile);
        return EXIT_FAILURE;
    }

    std::vector<uint8_t> framebuf(avpicture_get_size(dst_pix_fmt, dst_width, dst_height));
    avpicture_fill(reinterpret_cast<AVPicture*>(frame), framebuf.data(), dst_pix_fmt, dst_width, dst_height);

    decframe = av_frame_alloc();
    if(decframe ==NULL){
        av_frame_free(&decframe);
        avcodec_close(pCodecCtx);
        fclose(pFile);
        return EXIT_FAILURE;
    }

    //Create packet
    av_init_packet(&packet);


    // Decoding Loop
    do {

        //allocate packet memory
        if(av_new_packet(&packet,42000)) {
            break;
        }

        if (!end_of_stream) {
            // read packet from input file
            ret = read_next_frame(pFile, packet.size, (char *) packet.data);
            if (ret < 0) {
                std::cerr << "fail to av_read_frame: ret=" << ret;
                end_of_stream = true;
            }
            if (ret == 0 ) {
                std::cout << "PACKET SIZE IS ZERO ";
                end_of_stream = true;
            }
            packet.size = ret;
        }

        if (end_of_stream) {
            // null packet for bumping process
            av_init_packet(&packet);
            packet.data = nullptr;
            packet.size = 0;
        }

        // decode video frame
        avcodec_decode_video2(pCodecCtx, decframe, &got_pic, &packet);

        if (!got_pic)
            goto next_packet;

        // convert frame to OpenCV matrix
        sws_scale(swsctx, decframe->data, decframe->linesize, 0, decframe->height, frame->data, frame->linesize);
        {
            cv::Mat image(dst_height, dst_width, CV_8UC3, framebuf.data(), frame->linesize[0]);

#ifdef USE_VIDEO_SYNC
            if(last_frame_display_time) {
               // assuming that time only increments, never decrements
               uint64_t time_lapsed = av_gettime() - last_frame_display_time;

               int64_t  delay_required = (1000000/video_fps) - time_lapsed;
               if(delay_required > 0) {
                   av_usleep(delay_required);
               }
            }
#endif

            last_frame_display_time = av_gettime();
            cv::imshow("press ESC to exit", image);

            if (cv::waitKey(1) == 0x1b)
                break;

        }

        std::cout << nb_frames << '\r' << std::flush;  // dump progress
        ++nb_frames;

next_packet:
        // free packet memory
        av_free_packet(&packet);

    } while (!end_of_stream || got_pic);

    std::cout << nb_frames << " frames decoded" << std::endl;

    av_frame_free(&frame);
    av_frame_free(&decframe);
    avcodec_close(pCodecCtx);
    fclose(pFile);

    return retVal;
}

catch (const rs2::error & e)
{
    std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
    return EXIT_FAILURE;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}



