#include <stdio.h>

extern "C"
{
    #include <libavutil/timestamp.h>
    #include <libavformat/avformat.h>
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
    char buff[AV_TS_MAX_STRING_SIZE] = {0};
    printf("%s: pts:%d dts:%d duration:%d stream_index:%d\n",
        tag,
        pkt->pts,
        pkt->dts,
        pkt->duration,
        pkt->stream_index);
}

extern "C" __attribute__((visibility ("default")))
long aviDuration(const char * url)
{
    AVFormatContext *ifmt_ctx = NULL ;

    av_register_all();
	if (avformat_open_input(&ifmt_ctx, url, NULL, NULL) != 0)
        return -1;
    long d =ifmt_ctx->duration;
    avformat_close_input(&ifmt_ctx);
    return d;
}

extern "C" __attribute__((visibility("default")))
int aviCopy(const char *target, const char *source)
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    int stream_index = 0;
    int *stream_mapping = NULL;
    int stream_mapping_size = 0;
    int ret;

    av_register_all();

    if (avformat_open_input(&ifmt_ctx, source, NULL, NULL) != 0)
        goto end;

    if (avformat_find_stream_info(ifmt_ctx, NULL) < 0)
        goto end;
    
    // av_dump_format(ifmt_ctx, 0, source, 0);

    avformat_alloc_output_context2(&ofmt_ctx, NULL, "mp4", target);
    if (!ofmt_ctx)
    {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    stream_mapping_size = ifmt_ctx->nb_streams;
    stream_mapping = (int*)av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping)
    {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

    for (int i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
        {
            stream_mapping[i] = -1;
            continue;
        }

        stream_mapping[i] = stream_index++;
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream)
        {
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0)
        {
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    // av_dump_format(ofmt_ctx, 0, target, 1);

    if (!(ofmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&ofmt_ctx->pb, target, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            goto end;
        }
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0)
    {
        goto end;
    }


    while (true)
    {
        AVStream *in_stream, *out_stream;
        int64_t start_time, offset;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;
        
        in_stream = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0)
        {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index == 0)
            log_packet(ifmt_ctx, &pkt, "in");

        /* copy packet */
        start_time = ifmt_ctx->streams[pkt.stream_index]->start_time;
        offset =av_rescale_q(start_time, in_stream->time_base, out_stream->time_base);
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX)) - offset;
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX)) - offset;
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        if (pkt.stream_index == 0)
            log_packet(ofmt_ctx, &pkt, "out");
        
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0)
        {
            break;
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmt_ctx);
end:
    avformat_close_input(&ifmt_ctx);

    /* close output */
    if (ofmt_ctx && !(ofmt_ctx->flags && AVFMT_NOFILE))
    {
        avio_closep(&ofmt_ctx->pb);
    }
    avformat_free_context(ofmt_ctx);
    av_freep(&stream_mapping);

    if (ret < 0 && ret != AVERROR_EOF)
    {
        return 1;
    }

    return 0;
}

int main()
{
    aviCopy("/home/suxi/Videos/out.mp4","/home/suxi/Videos/1504100149.ts");
    return 0;
}