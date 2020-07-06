# pipe_flv2annexb
Writes raw video to FFmpeg stdin PIPE, reads encoded FLV H.264 from stdout PIPE and convert to Annex B standart.

The project includes 3 implemntations that performes the same thing:
1. **pipe_flv2annexb.py** - Python 3 implementation.
2. **main_windows.cpp** - C++ implementation for Windows OS.
3. **main_linux.cpp** - C++ implementation for Linux OS.

#### Here is a detailed description of main_linux.cpp implementation:

Stream raw video frames to FFmpeg stdin PIPE,  
FFmpeg encodes the video in H.264 codec and FLV (Flash Video) container format.  
The code reads the FLV encoded data from FFmpeg stdout PIPE,  
the AVC NAL units are extracted and converted from AVCC to Annex B format.  
The output is a file containing H.264 elementary stream (in Annex B format).  

It is recommended to try the Python implementation first (pipe_flv2annexb.py),  
before trying to understand the C++ implementation.

What is the purpose?  
The current implementation is a "proof of concept" - proves that the solution is viable.  
Using PIPE interface to FFmpeg may be useful for prototyping, simulations and demonstrations.  
Note: The implementation stores the output to a file for testing  
The true purpose is to transmit the stream (over TCP/UDP for example).
	  
The described concept is useful when:  
1. Video encoding latency is important.
2. It is required to get the encoded data frame by frame.  
   Splitting the stream to "encoded frames" is required when customization is needed.  
   Example: Attaching a packet of meta data for each "encoded frame" (then streaming out the result).
   
Requirements:  
The following implementation is compatible with Linux (tested under Ubuntu 18.04 64-bit).  
[Windows implementation can be found in main.cpp (merging the implementations is relatively simple)].  
The implementation was tested with FFmpeg version 4.3-static.  
A static build of FFmpeg may be downloaded from: https://johnvansickle.com/ffmpeg/  
For testing, place the ffmpeg executable in the same path as the application (same directory as the ".out" file).  
You may also use default FFmpeg version of Ubuntu 18.04: Version 3.4.6-0ubuntu0.18.04.1 (use /usr/bin/ffmpeg instead of ./ffmpeg').  
OpenCV is used for building synthetic video frames with sequential numbering (used only for testing).  
Installing OpenCV with C++ libraries: sudo apt install libopencv-dev (see: https://linuxconfig.org/install-opencv-on-ubuntu-18-04-bionic-beaver-linux).  

Building:  
*I am using Console Application (Linux) project under Visual Studio 2017 (using SSH protocol with Ubuntu 18.04 installed in Virtual Box)*  
You may use the following command-line for building main_linux.cpp (the output is an executable file: pipe_flv2annexb_linux.out):  

    g++ -o "pipe_flv2annexb_linux.out" "main_linux.cpp" -lopencv_core -lopencv_imgproc -lopencv_highgui -std=c++11 -Wall -fexceptions  

Why FFmpeg?  
1. FFmpeg is free, and open sourced (LGPL 3.0 license, but ffmpeg static build is GPL 3.0).
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

```
 Input:        ---------   Output:  
 raw video    | FFmpeg  |  Encoded video  
------------> | Process | --------------> AVC Encoded stream in FLV container.  
 stdin PIPE    ---------   stdout PIPE    
```

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
Annex B format: `([start code] NALU) | ( [start code] NALU)`  
AVCC format: `([length] NALU) | ([length] NALU)`  
In Annex B, `[start code] may be 0x000001 or 0x00000001`.  
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
```
[Start of Last Packet][ Type ][Payload Length][timestamp][timestamp upper][identifier][Packet Payload]  
	  4 Bytes          1 Byte     3 Byes         3 Byes      1 Byte         3 Bytes
```

AVC Packet Payload structure:
```
[Frame Type and Codec ID][AVC Packet Type][Composition Time][  Data  ]
		    1 Byte               1 Byte           3 Bytes        n Bytes
```

See: https://en.wikipedia.org/wiki/Flash_Video  
And: https://www.adobe.com/content/dam/acom/en/devnet/flv/video_file_format_spec_v10.pdf

AVC Payload Data Structure (AVCC format NAL unit/units):  
May be few NAL units: `[SPS][PPS][Coded slice]`  (Each NAL unit starts with 4 bytes length)  
Or one NAL unit: `[Coded slice]`                 (The NAL unit starts with 4 bytes length)  

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

Tested using FFmpeg 4.3-static under Ubuntu 18.04 64-bit
