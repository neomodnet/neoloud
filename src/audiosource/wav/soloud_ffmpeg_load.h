/*
SoLoud audio engine - ffmpeg library loader/unloader
Copyright (c) 2025 William Horvath

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/

#ifndef SOLOUD_FFMPEG_LOAD_H
#define SOLOUD_FFMPEG_LOAD_H

#include <string>

#if defined(WITH_FFMPEG) && __has_include(<libavcodec/avcodec.h>) && (defined(WITH_SDL3) || ((defined(_WIN32) || defined(_WIN64)) || defined(__linux__)))

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace SoLoud::FFmpeg::FFmpegLoader::FFmpegFuncs
{
// define core function groups by library
#define AVFORMAT_FUNCTIONS(X) \
	X(av_read_frame) \
	X(avformat_alloc_context) \
	X(avformat_close_input) \
	X(avformat_find_stream_info) \
	X(avformat_open_input) \
	X(avformat_seek_file) \
	X(avio_alloc_context) \
	X(avio_context_free)

#define AVCODEC_FUNCTIONS(X) \
	X(av_packet_alloc) \
	X(av_packet_free) \
	X(av_packet_unref) \
	X(avcodec_alloc_context3) \
	X(avcodec_find_decoder) \
	X(avcodec_flush_buffers) \
	X(avcodec_free_context) \
	X(avcodec_open2) \
	X(avcodec_parameters_to_context) \
	X(avcodec_receive_frame) \
	X(avcodec_send_packet)

#define AVUTIL_FUNCTIONS(X) \
	X(av_channel_layout_copy) \
	X(av_channel_layout_default) \
	X(av_channel_layout_uninit) \
	X(av_dict_free) \
	X(av_dict_set) \
	X(av_frame_alloc) \
	X(av_frame_free) \
	X(av_frame_unref) \
	X(av_free) \
	X(av_log_set_callback) \
	X(av_log_format_line2) \
	X(av_log_set_level) \
	X(av_log_get_level) \
	X(av_malloc) \
	X(av_opt_set_chlayout) \
	X(av_opt_set_int) \
	X(av_opt_set_sample_fmt) \
	X(av_rescale_q) \
	X(av_sample_fmt_is_planar)

#define SWRESAMPLE_FUNCTIONS(X) \
	X(swr_alloc) \
	X(swr_convert) \
	X(swr_drop_output) \
	X(swr_free) \
	X(swr_get_delay) \
	X(swr_init)

#define ALL_FFMPEG_FUNCTIONS(X) \
	AVFORMAT_FUNCTIONS(X) \
	AVCODEC_FUNCTIONS(X) \
	AVUTIL_FUNCTIONS(X) \
	SWRESAMPLE_FUNCTIONS(X)

// import types we need
using ::AVChannelLayout;
using ::AVCodec;
using ::AVCodecContext;
using ::AVCodecParameters;
using ::AVDictionary;
using ::AVFormatContext;
using ::AVFrame;
using ::AVIOContext;
using ::AVPacket;
using ::AVStream;
using ::SwrContext;

// import constants that we need
using ::AV_SAMPLE_FMT_FLTP;
using ::AVMEDIA_TYPE_AUDIO;

// generate function pointer types and declarations
#define DECLARE_FFMPEG_FUNCTION(name) \
	using name##_t = decltype(&::name); \
	extern name##_t name;

ALL_FFMPEG_FUNCTIONS(DECLARE_FFMPEG_FUNCTION)

#undef DECLARE_FFMPEG_FUNCTION

} // namespace SoLoud::FFmpeg::FFmpegLoader::FFmpegFuncs

#endif
#endif // SOLOUD_FFMPEG_H
