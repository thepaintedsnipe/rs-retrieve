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


/**
* @brief seeks the pFile to next packet boundary
* @details at 10 mbps, 30fps, each frame will be about 42K byes
*  search_size could be specified accordingly
*
* @param[in] pFile Pointer to the file containing encoded data
* @param[in] search_size is the max no of bytes to look for
* @return 0 if ok, -1 if there was no frame found
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

/**
* @brief reads next compressed video packet in to a buffer
* @details it is assumed that pFile is already at packet boundary
*  data till next packet is read in to buffer
*
* @param[in] pFile Pointer to the file containing encoded data
* @param[out] buf Pointer to buf where packet data is copied
* @param[in] buf_size Size of the data buffer
* @return packet size or -1 if error
*/
static int  read_video_packet(FILE* pFile, int buf_size, char *buf){

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

// various globals

// video codec parameters
int video_param_height;
int video_param_width;
int video_param_fps;

FILE* pFile;
AVCodec *vcodec = 0;
AVCodecContext *pCodecCtx;
AVPacket packet;


static void read_video_parameters()
{
    // we need to know width, height & fps
    // as of now hard-coded values
    // but these could from a txt file
    video_param_width = 640;
    video_param_height = 480;
    video_param_fps = 30;
}


/**
* @brief initialize decoder instance
*
* @return 0 on success, error code on failure
*/
int initialize_decoder(char *infile, int *width, int *height, int *fps, AVPixelFormat *pix_fmt)
{
    int ret;

    pFile = fopen (infile, "rb");

    if(pFile == NULL) {
        std::cout << "Can not open file for writing" << std::endl;
        return 1;
    }

    if (seek_to_frame(pFile, 42000) != 0) {
        std::cout << "Failed to find a valid encoded frame" << std::endl;
        fclose(pFile);
        return 1;
    }

    // read the video parameters
    read_video_parameters();

    *width = video_param_width;
    *height = video_param_height;
    *fps = video_param_fps;
    *pix_fmt  = AV_PIX_FMT_YUV420P;

    // initialize FFmpeg library
    av_register_all();
    avformat_network_init();

    // Check codec support
    vcodec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!vcodec)
    {
        std::cout << "Unable to find video encoder" << std::endl;
        return 1;
    }

    pCodecCtx = avcodec_alloc_context3(vcodec);
    if (!pCodecCtx)
    {
        std::cout << "Unable to allocate encoder context" << std::endl;
        return 2;
    }

    pCodecCtx->width = video_param_width;
    pCodecCtx->height= video_param_height;
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

    // open the decoder
    ret = avcodec_open2(pCodecCtx, vcodec, NULL);
    if (ret < 0) {
        std::cerr << "fail to avcodec_open2: ret=" << ret;
        return 3;
    }

    //Create packet
    av_init_packet(&packet);

    return 0;
}

/**
* @brief free decoder instance
*
* @return none
*/
void free_decoder()
{
    fclose(pFile);
    avcodec_close(pCodecCtx);
}

/**
/* @brief return a decoded video frame
/*
/* @param[in] decodedFrame pointer to AVFrame where decoded video is copied
/*
/* returns 0 if a valid frame was decoded
/*         1 if end of stream
/*         2 if any other issue
*/
int get_decoded_frame(AVFrame* decodedFrame)
{
    int got_pic = 0;
    int retVal = 0;
    int status;
    bool end_of_stream = false;

    do {

        //allocate packet memory
        if(av_new_packet(&packet,42000)) {
            retVal = 2;
            break;
        }

        if (!end_of_stream) {
            // read packet from input file
            status = read_video_packet(pFile, packet.size, (char *) packet.data);
            if (status < 0) {
                std::cerr << "fail to av_read_frame: status=" << status  << std::endl;
                retVal = 1;
                end_of_stream = true;
            }
            if (status == 0 ) {
                std::cout << "PACKET SIZE IS ZERO ";
                retVal = 1;
                end_of_stream = true;
            }
            packet.size = status;
        }

        if (end_of_stream) {
            std::cerr << "end of stream found so set packet to null" << status  << std::endl;
            // null packet for bumping process
            av_init_packet(&packet);
            packet.data = nullptr;
            packet.size = 0;
        }

        // decode video frame
        avcodec_decode_video2(pCodecCtx, decodedFrame, &got_pic, &packet);

        // free packet memory
        av_free_packet(&packet);

        if(got_pic)
            break;

    } while(end_of_stream);

    return retVal;
}


