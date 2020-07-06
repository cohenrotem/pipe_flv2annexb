#!/usr/bin/env python
# -*- coding: utf-8 -*-

""" Stream raw video frames to FFmpeg stdin PIPE,
    FFmpeg encodes the video in H.264 codec and FLV (Flash Video) container format.
    The code reads the FLV encoded data from FFmpeg stdout PIPE,
    the AVC NAL units are extracted and converted from AVCC to Annex B format.
    The output is a file containing H.264 elementary stream (in Annex B format).
    
    What is the purpose?
    The Python implementation is a "proof of concept", for C++ implementation.
    Using PIPE interface to FFmpeg may be useful for prototyping, simulations and demonstrations.
    Note: The implementation stores the output to a file for testing
          The true purpose is to transmit the stream (over TCP/UDP for example).
    
    The described concept is useful when:
    1. Video encoding latency is important.
    2. It is required to get the encoded data frame by frame.
       Splitting the stream to "encoded frames" is required when customization is needed.
       Example: Attaching a packet of meta data for each "encoded frame" (then streaming out the result).
       
    Requirements:
    1. Python 3.6 or above.
    2. FFmpeg executable must be in the execution path.   
       
    Why FFmpeg?
    1. FFmpeg is free, and open sourced (LGPL 3.0 license, but ffmpeg.exe is GPL 3.0).
    2. FFmpeg is an extremely powerful and versatile tool for video encoding (and more).
    3. FFmpeg has an extensive documentation, and many results in Google...
    4. FFmpeg may be used as single statically linked executable (no setup needed).
    5. FFmpeg is supported by Windows, Linux flavors, x86, ARM (and other platforms).
    See: https://ffmpeg.org/
    
    Why PIPE interface?
    1. PIPE is a generic solution for inter-process communication:
       PIPE interface is supported by Windows, Linux (and more).
       PIPE interface is supported by most relevant programming languages.
    2. Using PIPE interface is usually safer compared to using dynamic linking.
    3. Using FFmpeg with PIPE interface is more simple then using FFmpeg C interface.
    4. Using PIPE interface doesn't break the GPL 3.0 license.
    See: http://zulko.github.io/blog/2013/09/27/read-and-write-video-frames-in-python-using-ffmpeg/
    And: https://batchloaf.wordpress.com/2017/02/12/a-simple-way-to-read-and-write-audio-and-video-files-in-c-using-ffmpeg-part-2-video/
           
     Input:        ---------   Output:        
     raw video    | FFmpeg  |  Encoded video  
    ------------> | Process | --------------> AVC Encoded stream in FLV container.
     stdin PIPE    ---------   stdout PIPE     
        
    Why H.264 (AVC)?
    1. In most cases, H.264 format is a system requirement.
    2. Most platforms supports H.264 hardware acceleration.
       [Unfortunately FFmpeg does not support H.265 encoded FLV].
       
    Why FLV container?
    1. FLV container is simple, and well documented.
    2. FLV container is designed for streaming (opposed to MP4 [for example] that designed as file container).
    3. In FLV, every payload ("access unit") starts with "Payload Size".
       The size information is useful when reading from a PIPE (a way to know how many bytes to read).
       Knowing the size is the main reason for using a container
       (in H.264 elementary stream there is no size information when reading from a PIPE).
    
    FLV container is selected because it is relatively simple to parse.
    The advantage of using FLV over elementary H.264 stream is the "Payload Size" information.
    H.264 Annex B encoded packets doesn't have packet size information.
    The only way to know when packet ends, is when next packet begins.
    The lack of size information adds a latency of one frame to the "pipeline".
    The lack of size information also makes it difficult to split stream into packets.
    Note: There is no method for querying the number of bytes in the PIPE.
    Note: Reading byte by byte from stdout PIPE may solve the issues, but it is very inefficient.
    
    NAL units:
    AVC (H.264) encoded video stream is separated into NAL units.
    NAL refers to Network Abstraction Layer.
    See: https://en.wikipedia.org/wiki/Network_Abstraction_Layer
    An encoded video frame "access unit" may be built from one or few NAL units.
    In streaming video each keyframe (IDR frame) begins with SPS and PPS (data) NAL units,
    then comes the "encoded data" NAL unit.
    Other frames (P/B frames) are encoded as a single NAL unit.
    See: https://yumichan.net/video-processing/video-compression/introduction-to-h264-nal-unit/
    
    NAL units in FLV container:
    FLV file/stream is divided into block names "FLV tags".
    FLV Payload Data of one Tag applies one "access unit" (encoded video frame with one/few NAL units).
    When reading FLV encoded data from stdout PIPE, we can read the Payload Size from Payload header first, 
    and then read the expected number of bytes from the PIPE.
    
    AVCC versus Annex B:
    FLV container stores the NAL units in AVCC format.
    The H.264 elementary stream (our output) must be stored in Annex B format.
    The code below, converts the NAL units from AVCC to Annex B format (among other things).
    Assume NALU is a basic unit.
    Annex B format: ([start code] NALU) | ( [start code] NALU)
    AVCC format: ([length] NALU) | ([length] NALU)
    In Annex B, [start code] may be 0x000001 or 0x00000001.
    For Annex B and AVCC format, the NALUs are the same.
    See: https://stackoverflow.com/questions/23404403/need-to-convert-h264-stream-from-annex-b-format-to-avcc-format
    And: http://neurocline.github.io/dev/2016/07/28/video-and-containers.html
    
    Adjusting stream format by setting FFmpeg flags:
    The following FFmpeg flags are used for removing some irrelevant data:
        "-flvflags +no_sequence_end+no_metadata+no_duration_filesize"
        The flags removes the file "footer" and some irrelevant metadata.
    The following FFmpeg flags are used for adding additional data: "-bsf:v dump_extra"
        The "dump_extra" flag, forces the repetition of SPS and PPS NAL units for every key frame.
        (Opposed to placing the SPS and PPS only once at the beginning of the file).
        When using a file, there is no need to duplicate the SPS and PPS information.
        When using a stream (i.e UDP streaming), there is no guarantee that the receiver 
        receives the beginning of the stream (it needs to re-sync on next key frame).       

    FLV (Flash Video) container format:    
    FLV stream begins with three letters "FLV" (we may use it for "sanity check").
    The stream starts with a short header and data, that is irrelevant in our case.
    After the header comes FLV body.
    The FLV body contains the AVC encoded stream, arranged in "FLV tags"
    Each "FLV tag" stats with a small header followed by AVC "access unit" payload.
    
    FLV Tag Structure:
    [Start of Last Packet][ Type ][Payload Length][timestamp][timestamp upper][identifier][Packet Payload]
          4 Bytes          1 Byte     3 Byes         3 Byes      1 Byte         3 Bytes
    
    AVC Packet Payload structure:
    [Frame Type and Codec ID][AVC Packet Type][Composition Time][  Data  ]
             1 Byte               1 Byte           3 Bytes        n Bytes
    
    See: https://en.wikipedia.org/wiki/Flash_Video
    And: https://www.adobe.com/content/dam/acom/en/devnet/flv/video_file_format_spec_v10.pdf
    
    AVC Payload Data Structure (AVCC format NAL unit/units):
    May be few NAL units: [SPS][PPS][Coded slice]  (Each NAL unit starts with 4 bytes length)
    Or one NAL unit: [Coded slice]                 (The NAL unit starts with 4 bytes length)
    
    Converting the "FLV stream" to H.264 elementary stream:
    1. The FLV "file header" is read from the PIPE, and ignored.
    2. Read FLV Tag header, and extract Payload Length.
       Repeat the process until stdout PIPE is closed:
    2.1 Read NAL units until number of read bytes reaches Payload Length:
    2.1.1 Read NAL unit length.
    2.1.2 Read NAL unit data (according to length).
    2.1.3 Convert the NAL unit format from AVCC to Annex B (replace length with 0x00000001 or 0x000001).
          The Annex B NAL units are written to a file (for testing).
          
    How to test:
    The output is written to a file.
    The file is compared to H.264 elementary stream file (encoded with the same set of encoder parameters).
    The content of both files should be the same.
    
    Tested using FFmpeg 4.2.2 under Windows 10
    """
    

import cv2  # Used for building synthetic video frames (for testing only).
import numpy as np  # Used for building synthetic video frames (for testing only).
import subprocess as sp
import shlex


width, height, n_frames, fps = 1280, 720, 100, 25  # 100 frames, resolution 1280x720, and 25 fps


def make_raw_frame_as_bytes(i):
    """ Build synthetic "raw BGR" image for testing, convert the image to bytes sequence """
    p = width//60  # Font scale (p) and thickness (p*2) is proportional to image width.
    img = np.full((height, width, 3), 60, np.uint8)

    # Random BGR image in resolution width by height.
    # img = np.random.randint(0, 99, (height, width, 3), np.uint8)
    #img = np.random.randint(60, 64, (height, width, 3), np.uint8)

    text = str(i+1)
    (tsize_x, tsize_y), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_DUPLEX, p, p*2)

    cv2.putText(img, text, ((width-tsize_x)//2, (height+tsize_y)//2), cv2.FONT_HERSHEY_DUPLEX, p, (255, 30, 30), p*2)  # Blue number (centered).

    raw_img_bytes = img.tobytes()

    return raw_img_bytes


# https://www.programiz.com/python-programming/user-defined-exception
# Python user-defined exceptions
class FlvError(Exception):
    """ FLV bad format or unsupported format exception """

    def __init__(self, message):
        self.message = message
        super().__init__(self.message)

    def __str__(self):
        return self.message


def ReadFlvFileHeaderAndFirstPayload(f):
    """ FLV files start with a standard header (9 bytes).
        After the header comes the first payload, which contains irrelevant data.
        The function reads the header and the first payload data. """

    # https://en.wikipedia.org/wiki/Flash_Video
    flv_signature = f.read(3)

    if flv_signature != b'FLV':
        raise FlvError('Bad signature')  # FLV file must start with "FLV" letters.

    version_byte = f.read(1)

    if version_byte[0] != 1:
        raise FlvError('Bad version')  # Version byte must be 1

    flags_byte = f.read(1)

    if flags_byte[0] != 1:
        raise FlvError(f'flags_byte = {flags_byte} ... Bitmask: 0x04 is audio, 0x01 is video (so 0x05 is audio+video), but we expect video only')

    header_size = f.read(4) # Used to skip a newer expanded header
    
    # Read first packet (and ignore it).
    flv_payload_size = ReadFlvPacketHeader(f)

    # Read "frame_type and _codec_id" byte for testing.
    frame_type_and_codec_id = f.read(1)
    codec_id = int(frame_type_and_codec_id[0]) & 0xF
    frame_type = int(frame_type_and_codec_id[0]) >> 4  # 1 - keyframe, 2 - inter frame
    
    if codec_id != 7:
        raise FlvError(f'Codec ID is not AVC. codec_id = {codec_id}, instead of 7')
         
    # Read AVC packet type for testing
    avc_packet_type = f.read(1)  # 0 - AVC sequence header, 1 - AVC NALU, 2 - AVC end of sequence
    
    if avc_packet_type[0] != 0:
        raise FlvError(f'First packet is supposed to be AVC sequence header, but avc_packet_type = {avc_packet_type}, instead of 0')
    
    payload_data = f.read(flv_payload_size-2)  # Data as defined by packet type



def ReadFlvPacketHeader(f):
    """ Read header of FLV packet and return FLV payload size
        After the header, the file is split into packets called "FLV tags", 
        which have 15-byte packet headers. 
        The first four bytes denote the size of the previous packet/tag
        (including the header without the first field), and aid in seeking backward """
    size_of_previous_packet = f.read(4)  # For first packet set to NULL (uint32 big-endian)
    packet_type = f.read(1)  # For first packet set to AMF Metadata
    flv_payload_size = f.read(3)  # For first packet set to AMF Metadata (uint24 big-endian)
    timestamp_lower = f.read(3)  # For first packet set to NULL (uint24 big-endian)
    timestamp_upper = f.read(1)  # Extension to create a uint32_be value (0)
    stream_id = f.read(3)  # For first stream of same type set to NULL  (uint24 big-endian)

    if len(flv_payload_size) < 3:
        return 0  # End of file.

    # Convert uint24 big-endian to integer value
    return (int(flv_payload_size[0]) << 16) + (int(flv_payload_size[1]) << 8) + int(flv_payload_size[2])



def ReadPacket5BytesHeader(f):
    """ Read the 5 bytes of FLV packet header, and return codec_id """
    # https://www.adobe.com/content/dam/acom/en/devnet/flv/video_file_format_spec_v10.pdf
    frame_type_and_codec_id = f.read(1)

    codec_id = int(frame_type_and_codec_id[0]) & 0xF
    frame_type = int(frame_type_and_codec_id[0]) >> 4  # 1 - keyframe, 2 - inter frame

    # According to video_file_format_spec_v10.pdf, if codec_id = 7, next comes AVCVIDEOPACKET
    avc_packet_type = f.read(1)  # 0 - AVC sequence header, 1 - AVC NALU, 2 - AVC end of sequence
    composition_time = f.read(3)

    if avc_packet_type[0] != 1:
         raise FlvError(f'avc_packet_type = {avc_packet_type}')

    return codec_id



def ReadFlvPayloadAndConvertToAnnexB(f):
    """Read FLV payload, and convert it to AVC Annex B format.
       Return Annex B data as bytes array (return None if end of file).
       The FLV payload may contain several AVC NAL units (in AVCC format).
       Return empty if reached end of file. 
       https://yumichan.net/video-processing/video-compression/introduction-to-h264-nal-unit/ """
    
    # annexb_payload is the "encoded frame" converted from AVCC to Annex B format.
    annexb_payload = b''

    # Read FLV packet header
    flv_payload_size = ReadFlvPacketHeader(f)
    
    if flv_payload_size == 0:
        return None  # End of file.

    codec_id = ReadPacket5BytesHeader(f)

    if codec_id != 7:
        raise FlvError(f'Codec ID is not AVC. codec_id = {codec_id}, instead of 7')

    flv_payload_size -= 5  # After reading the "5 Bytes Header", remaining size is 5 bytes less

    # https://www.adobe.com/content/dam/acom/en/devnet/flv/video_file_format_spec_v10.pdf
    # Next comes "Data"
    # if AVCPacketType == 0 AVCDecoderConfigurationRecord
    # else if AVCPacketType == 1 One or more NALUs (can be individual slices per FLV packets; that is, full frames are not strictly required)
    # else if AVCPacketType == 2 Empty
        
    # Keep reading AVC NAL units, until flv_payload_size is zero (flv_payload_size holds the remaining size).
    while flv_payload_size > 0:
        nal_size = f.read(4)  # Read NAL unit size (uin32, big endian).
        flv_payload_size -= 4  # Remaining size is 4 bytes less

        if nal_size is None:
            return None  # End of file.

        # Convert uint32 big-endian to integer value
        nal_size = (int(nal_size[0]) << 24) + (int(nal_size[1]) << 16) + (int(nal_size[2]) << 8) + int(nal_size[3])

        nal_data = f.read(nal_size)  # Read NAL unit data
        flv_payload_size -= nal_size  # Remaining size is nal_size bytes less

        # The number of leading zeros (2 or 3) has minor differences between encoders (the implementation tries to match the selected encoder).
        if do_use_intel_quick_sync:
            if ((nal_data[0] & 0xF) == 5) or (nal_data[0] & 0xF == 1):
                # Coded slice of IDR picture and Coded slice of non IDR picture (for some reason begins with only 2 zeros when encoding with Intel QuickSync)
                # SEI NAL unit (nal_data[0] == 1) is also begin with only 2 zeros.
                annexb_payload_head = b'\x00\x00\x01'  # Concatenate [0, 0, 1] to end of annexb_payload, and then NAL data
            else:
                # Other NAL units begins with 0 0 0 0 1 (for matching FFmpeg Annex B encoded stream)
                annexb_payload_head = b'\x00\x00\x00\x01'  # Concatenate [0, 0, 0, 1] to end of annexb_payload, and then NAL data
        else:
            if ((nal_data[0] & 0xF) == 5) or (nal_data[0] & 0xF == 6):
                # Coded slice of an IDR picture (for some reason begins with only 2 zeros when encoding with libx264)
                # SEI NAL unit (nal_data[0] == 6) is also begin with only 2 zeros.
                annexb_payload_head = b'\x00\x00\x01'  # Concatenate [0, 0, 1] to end of annexb_payload, and then NAL data
            else:
                # Other NAL units begins with 0 0 0 0 1 (for matching FFmpeg Annex B encoded stream)
                annexb_payload_head = b'\x00\x00\x00\x01'  # Concatenate [0, 0, 0, 1] to end of annexb_payload, and then NAL data
        
        # Concatenate NAL data in Annex B format to annexb_payload
        annexb_payload = annexb_payload + annexb_payload_head + nal_data

    return annexb_payload
 


do_test_zero_latency = True  # Set to True for testing latency of zero frames (applies latency added by the encoder).

# Stream FLV H.264 encoded video to stdout PIPE (the input is raw BGR video streamed to stdin PIPE).
# -flvflags +no_sequence_end+no_metadata+no_duration_filesize  -  removes redundant FLV metadata
# -bsf:v dump_extra  -  repeat SPS and PPS (and SEI) NAL units for every key frame.
# -g 25 -bf 3  -  GOP size of 25 frames, and encode video with 3 B-Frames
# -threads 1  -  Use single thread (reduce latency in some cases).

if do_test_zero_latency:
    n_frames_latency = 0  # Latency of zero frames

    do_use_intel_quick_sync = False  # Using Intel Quick requires Hardware and Drivers support.

    if do_use_intel_quick_sync:
        ffmpeg_cmd = f'ffmpeg -hide_banner -threads 1 -framerate {fps} -video_size {width}x{height} -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec h264_qsv -async_depth 1 -forced_idr 1 -g 10 -bf 0 -b:v 20000k -pix_fmt nv12 -f flv -flvflags no_sequence_end+no_metadata+no_duration_filesize -bsf:v dump_extra -an -sn -dn pipe:'
    
        # FFmpeg sub-process with same arguments, but without FLV container, and save output to a file (instead of stdout PIPE) for testing.
        ffmpeg_test_cmd = f'ffmpeg -y -hide_banner -threads 1 -framerate {fps} -video_size {width}x{height} -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec h264_qsv -async_depth 1 -forced_idr 1 -g 10 -bf 0 -b:v 20000k -pix_fmt nv12 -f h264 -an -sn -dn out.264'
    else:
        ffmpeg_cmd = f'ffmpeg -hide_banner -threads 1 -framerate {fps} -video_size {width}x{height} -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 -x264-params bframes=0:force-cfr=1:no-mbtree=1:sync-lookahead=0:sliced-threads=1:rc-lookahead=0 -g 10 -pix_fmt yuv444p -crf 10 -f flv -flvflags no_sequence_end+no_metadata+no_duration_filesize -bsf:v dump_extra -an -sn -dn pipe:'
    
        # FFmpeg sub-process with same arguments, but without FLV container, and save output to a file (instead of stdout PIPE) for testing.
        ffmpeg_test_cmd = f'ffmpeg -y -hide_banner -threads 1 -framerate {fps} -video_size {width}x{height} -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 -x264-params bframes=0:force-cfr=1:no-mbtree=1:sync-lookahead=0:sliced-threads=1:rc-lookahead=0 -g 10 -pix_fmt yuv444p -crf 10 -f h264 -an -sn -dn out.264'

    
else:
    # Using the following setting results latency of many frames
    n_frames_latency = 26  # The exact value was found by trial and error

    do_use_intel_quick_sync = False

    ffmpeg_cmd = f'ffmpeg -hide_banner -threads 1 -framerate {fps} -video_size {width}x{height} -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 -g 25 -bf 3 -pix_fmt yuv444p -crf 10 -f flv -flvflags no_sequence_end+no_metadata+no_duration_filesize -bsf:v dump_extra -an -sn -dn pipe:'
        
    # FFmpeg sub-process with same arguments, but without FLV container, and save output to a file (instead of stdout PIPE) for testing.
    ffmpeg_test_cmd = f'ffmpeg -y -hide_banner -threads 1 -framerate {fps} -video_size {width}x{height} -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 -g 25 -bf 3 -pix_fmt yuv444p -crf 10 -f h264 -an -sn -dn out.264'


# Execute FFmpeg as sub-process.
ffmpeg_process = sp.Popen(shlex.split(ffmpeg_cmd), stdin=sp.PIPE, stdout=sp.PIPE)

# Execute another FFmpeg sub-process (used for testing).
ffmpeg_test_process = sp.Popen(shlex.split(ffmpeg_test_cmd), stdin=sp.PIPE)


flv_pipe = ffmpeg_process.stdout

out_f = None # Open output file (Annex B stream format)



for i in range(n_frames):
    raw_img_bytes = make_raw_frame_as_bytes(i)
    ffmpeg_process.stdin.write(raw_img_bytes)  # Write raw video frame to input stream of FFmpeg sub-process.
    ffmpeg_process.stdin.flush()

    ffmpeg_test_process.stdin.write(raw_img_bytes)
    ffmpeg_test_process.stdin.flush()

    if i == 0:
        ReadFlvFileHeaderAndFirstPayload(flv_pipe)  # Read FLV header that should be ignored.
        out_f = open('out_avcc.264', 'wb')  # Open output file (Annex B stream format)
   
    # Assume <n_frames_latency> frames latency
    # Note: the reading process is supposed to be in a separate thread (performed here for simplicity).
    if i >= n_frames_latency:
        # Read FLV payload data and convert the AVC NAL unit/units from AVCC format to Annex B format.
        annexb_payload = ReadFlvPayloadAndConvertToAnnexB(flv_pipe)

        # Write encoded frame to output file.
        # Note: "encoded frame" may contain few NAL units, but each FLV payload applies one "encoded frame" (one "access unit").
        out_f.write(annexb_payload)  # Write to file for testing.

ffmpeg_test_process.stdin.close()

ffmpeg_process.stdin.close()  # Closing stdin "pushes" all the remaining frames from the encoder to stdout (FFmpeg feature).

# Handle the last (delayed) "encoded frames".
for i in range(n_frames_latency):
    # Read FLV payload data and convert the AVC NAL unit/units from AVCC format to Annex B format.
    annexb_payload = ReadFlvPayloadAndConvertToAnnexB(flv_pipe)
    out_f.write(annexb_payload)  # Write to file for testing.
  

if out_f is not None:
    out_f.close()

extra_data_byte = flv_pipe.read(4)  # Read extra trailing 4 bytes (FFmpeg puts the 4 bytes as a footer [instead of a header of the next frame]).

ffmpeg_process.stdout.close()
ffmpeg_process.wait()

ffmpeg_test_process.wait()
