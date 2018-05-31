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


int initialize_decoder(char *infile, int *width, int *height, int *fps, AVPixelFormat *pix_fmt);
void free_decoder();
int get_decoded_frame(AVFrame* decodedFrame);

int main(int argc, char * argv[]) try
{
    using namespace cv;
    const auto window_name = "Display Image";

    AVFrame* frame;
    AVFrame* decframe;


#ifdef USE_VIDEO_SYNC
    uint64_t last_frame_display_time = 0;
#endif

    char* infile;

    unsigned nb_frames = 0;
    bool end_of_stream = false;

    int vcodec_width;
    int vcodec_height;
    int vcodec_fps;
    AVPixelFormat vcodec_pix_fmt;

    if (argc < 2) {
        std::cout << "Usage: rs-retrieve <infile>" << std::endl;
        return EXIT_FAILURE;
    }
    infile = argv[1];

    if (initialize_decoder(infile, &vcodec_width, &vcodec_height, &vcodec_fps, &vcodec_pix_fmt)) {
        return EXIT_FAILURE;
    }

    // initialize sample scaler
    SwsContext* swsctx = sws_getCachedContext(
        nullptr, vcodec_width, vcodec_height, vcodec_pix_fmt,
        vcodec_width, vcodec_height, AV_PIX_FMT_BGR24, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!swsctx) {
        free_decoder();
        return EXIT_FAILURE;
    }
    std::cout << "output: " << vcodec_width << 'x' << vcodec_height << ',' << av_get_pix_fmt_name(vcodec_pix_fmt) << std::endl;

    // 1. allocate "frame" for Displaying
    frame = av_frame_alloc();
    if(frame ==NULL){
        free_decoder();
        return EXIT_FAILURE;
    }

    std::vector<uint8_t> framebuf(avpicture_get_size(AV_PIX_FMT_BGR24, vcodec_width, vcodec_height));
    avpicture_fill(reinterpret_cast<AVPicture*>(frame), framebuf.data(), AV_PIX_FMT_BGR24, vcodec_width, vcodec_height);


    // 2. allocate "decframe"
    decframe = av_frame_alloc();
    if(decframe ==NULL){
        av_frame_free(&frame);
        free_decoder();
        return EXIT_FAILURE;
    }

    // Decoding Loop
    do {

        // decode video frame
        if(get_decoded_frame(decframe)){
            // either eos or any other error
            std::cout << "we failed to get decoded frame so quitting" << std::endl;
            break;
        }

        // convert frame to OpenCV matrix
        sws_scale(swsctx, decframe->data, decframe->linesize, 0, decframe->height, frame->data, frame->linesize);
        {
            cv::Mat image(vcodec_height, vcodec_width, CV_8UC3, framebuf.data(), frame->linesize[0]);

#ifdef USE_VIDEO_SYNC
            if(last_frame_display_time) {
               // assuming that time only increments, never decrements
               uint64_t time_lapsed = av_gettime() - last_frame_display_time;

               int64_t  delay_required = (1000000/vcodec_fps) - time_lapsed;
               if(delay_required > 0) {
                   av_usleep(delay_required);
               }
            }
            last_frame_display_time = av_gettime();
#endif

            cv::imshow("press ESC to exit", image);

            if (cv::waitKey(1) == 0x1b)
                break;

        }

        std::cout << nb_frames << '\r' << std::flush;  // dump progress
        ++nb_frames;
    } while (!end_of_stream);

    std::cout << nb_frames << " frames decoded" << std::endl;

    av_frame_free(&frame);
    av_frame_free(&decframe);

    free_decoder();

    return EXIT_SUCCESS;
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



