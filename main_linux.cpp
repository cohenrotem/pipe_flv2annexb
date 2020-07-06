// main_linux.cpp
// ---------------

/*
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
[I am using Console Application (Linux) project under Visual Studio 2017 (using SSH protocol with Ubuntu 18.04 installed in Virtual Box)]
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

Tested using FFmpeg 4.3-static under Ubuntu 18.04 64-bit
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <error.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h> //Used for setting PIPE buffer size

#include "opencv2/opencv.hpp"
#include "opencv2/highgui.hpp"

extern int errno;

//#define DO_TEST_ZERO_LATENCY  // Enable for testing FFmpeg with zero frames latency (raw frame in, encoded out...)
#undef DO_TEST_ZERO_LATENCY     // Allow latency of multiple frames (first encoded frame is ready after multiple input raw video frames enters).


// Build synthetic "raw BGR" image for testing, image data is stored in <raw_img_bytes> output data buffer.
// The synthetic video frame includes sequential numbering (as text).
// The drawn number equals i+1
static void MakeRawFrameAsBytes(int width, int height, int i, unsigned char raw_img_bytes[])
{
    int p = width / 60;
    cv::Mat img = cv::Mat(cv::Size(width, height), CV_8UC3, (void*)raw_img_bytes, width*3);
    img = cv::Scalar(60, 60, 60);

    std::string text = std::to_string(i + 1);

    int base_line;
    cv::Size tsize = cv::getTextSize(text, cv::FONT_HERSHEY_DUPLEX, (double)p, p * 2, &base_line);

    cv::putText(img, text, cv::Point((width-tsize.width)/2, (height+tsize.height)/2), cv::FONT_HERSHEY_DUPLEX, (double)p, cv::Scalar(255, 30, 30), p*2);  // Blue number
}


// Print error message and exits from the application.
void ErrorExit(const char *error_essage)
{
    fprintf(stderr, "%s\n", error_essage);
    exit(1);
}


// https://stackoverflow.com/questions/6171552/popen-simultaneous-read-and-write
// As already answered, popen works in one direction.
// If you need to read and write, You can create a pipe with pipe(), 
// span a new process by fork() and exec functions and then redirect its input and outputs with dup2().

// CSubprocess executes a child process with stdin and stdout pipes.
class CSubprocess
{
private:
    pid_t m_pid = 0;
    int m_inpipefd[2] = { 0, 0 };	// m_inpipefd applies stdin pipe of the child process.
    int m_outpipefd[2] = { 0, 0 };	// m_outpipefd applies stdout pipe of the child process.

    char *m_args_as_char        = nullptr;	// Holds command arguments as charter array (array is modified by strtok function).
    const char **m_args_list    = nullptr;	// Array of pointers to command arguments (each pointer points different argument).
    bool m_is_stdin_pipe        = false;	// true if stdin pipe is configured to be opened.
    bool m_is_stdout_pipe       = false;	// true if stdout pipe is configured to be opened.

	// Constructor is private.
	// Object can only be created by executing Popen (static member function).
	// Note: using a function that returns an object and returns a pointer is safer (because there is a high probability for the object creation to fail)
    CSubprocess()
    {
    }

    ~CSubprocess()
    {
        if (m_args_list != nullptr)
        {
            delete[] m_args_list;	// Free allocated memory
        }

        if (m_args_as_char != nullptr)
        {
            delete[] m_args_as_char;	// Free allocated memory
        }
    }


public:
    // Executes child process with (optional) stdin and stdout pipes.
	// Return pointer to CSubprocess object in case of success, and nullptr in case of failure.
    // https://stackoverflow.com/questions/12596839/how-to-call-execl-in-c-with-the-proper-arguments
    // cmd - may be full path (like "/usr/bin/ffmpeg").
    // process_name - Process name applies argv[0] (like "ffmpeg").
    // cmd_args - command arguments separated by spaces (like "-g 10 -pix_fmt yuv444p -crf 10").
	// Note: arguments with spaces (like "my video file.264") are not supported by current implementation.
    static CSubprocess *Popen(const std::string cmd, 
                              const std::string process_name,
                              const std::string cmd_args,
                              const bool is_stdin_pipe = false, const bool is_stdout_pipe = false, const int buf_size = 0)
    {
        CSubprocess *sp = new CSubprocess();	// A CSubprocess object is created.

        int sts;

        sp->m_args_as_char = new char[cmd_args.length() + 1];
        strcpy(sp->m_args_as_char, cmd_args.c_str());	//Copy cmd_args from std:string to character array (going to be cut to tokens later).

        sp->m_is_stdin_pipe     = is_stdin_pipe;
        sp->m_is_stdout_pipe    = is_stdout_pipe;
        
        // Count number of spaces in cmd_args (applies number of arguments).
        int n_spaces = (int)std::count(cmd_args.begin(), cmd_args.end(), ' ');

        // Allocate n_spaces + 10, (m_args_list[0] is process name, last one is NULL, and allocate few spares).
        sp->m_args_list = new const char*[n_spaces + 10];

        // First argument is process name
        sp->m_args_list[0] = process_name.c_str();

        // Split cmd_args by space.
        // http://www.cplusplus.com/reference/cstring/strtok/
        // Build list of pointers to arguments strings (zero terminated characters array).
        int arg_count = 1;
        char *pch = strtok(sp->m_args_as_char, " ");
        while (pch != NULL)
        {
            sp->m_args_list[arg_count] = pch;
            arg_count++;
            pch = strtok(NULL, " ");
        }

        // Argument list ends with NULL
        sp->m_args_list[arg_count] = NULL;

        if (is_stdin_pipe)
        {
            // Create a pipe for the child process's STDOUT.
            // https://linux.die.net/man/2/pipe
            // int pipe(int pipefd[2]);
            // pipe() creates a pipe, a unidirectional data channel that can be used for interprocess communication.
            // The array pipefd is used to return two file descriptors referring to the ends of the pipe.pipefd[0] refers to the read end of the pipe.
            // pipefd[1] refers to the write end of the pipe.
            // Data written to the write end of the pipe is buffered by the kernel until it is read from the read end of the pipe
            // Return Value: On success, zero is returned. On error, -1 is returned, and errno is set appropriately.
            sts = pipe(sp->m_outpipefd);

            if (sts != 0)
            {
                fprintf(stderr, "Error: fail to create pipe for stdout pipe, errno = %d.\n", errno);
                delete sp;
                return nullptr; // Returning nullptr  indication for an error.
            }            
        }

        if (is_stdout_pipe)
        {
            // Create a pipe for the child process's STDIN.
            sts = pipe(sp->m_inpipefd);

            if (sts != 0)
            {
                fprintf(stderr, "Error: fail to create pipe for stdin pipe, errno = %d.\n", errno);
                delete sp;
                return nullptr; // Returning nullptr  indication for an error.
            }
        }

        // Create the child process.
        // https://www.man7.org/linux/man-pages/man2/fork.2.html
        // fork() creates a new process by duplicating the calling process.
        // The new process is referred to as the child process.
        // The calling process is referred to as the parent process.
        // The child process is an exact duplicate of the parent process except for the following points:
        // ... (see documentation)
        // RETURN VALUE:
        // On success, the PID of the child process is returned in the parent, and 0 is returned in the child.
        // On failure, -1 is returned in the parent, no child process is created, and errno is set appropriately.       
        sp->m_pid = fork();

        if (sp->m_pid == (-1))
        {
            fprintf(stderr, "Error: fork failed, errno = %d.\n", errno);
            delete sp;
            return nullptr; // Returning nullptr  indication for an error.
        }

        // On success 0 is returned in the child.
        if (sp->m_pid == 0)
        {
            // The following code is executed in the child process
            //////////////////////////////////////////////////////////////////////////
            int child_sts;

            if (is_stdin_pipe)
            {
                // https://man7.org/linux/man-pages/man2/dup.2.html
                // duplicate a file descriptor
                // The dup() system call creates a copy of the file descriptor oldfd, using the lowest-numbered unused file descriptor for the new descriptor.
                // After a successful return, the old and new file descriptors may be used interchangeably...
                // The dup2() system call performs the same task as dup(), but instead of using the lowest-numbered unused file descriptor, it uses the file  descriptor number specified in newfd.
                //
                // RETURN VALUE:
                // On success, these system calls return the new file descriptor.
                // On error, -1 is returned, and errno is set appropriately.
                child_sts = dup2(sp->m_outpipefd[0], STDIN_FILENO);
                if (child_sts == (-1))
                {
                    fprintf(stderr, "Error: dup2 STDIN_FILENO failed, errno = %d.\n", errno); exit(-1);
                }

                // Close unused ends of pipe (Rotem).
                child_sts = close(sp->m_outpipefd[1]);
                if (child_sts == (-1))
                {
                    fprintf(stderr, "Error: close(sp->m_outpipefd[1]) failed in child process, errno = %d.\n", errno);
                }

                if (buf_size > 0)
                {
                    // Set pipe buffer size (could we do it  before executing dup2 ?).
                    // https://stackoverflow.com/questions/5218741/set-pipe-buffer-size
                    // https://man7.org/linux/man-pages/man2/fcntl.2.html
                    // fcntl - manipulate file descriptor.
                    // fcntl() performs one of the operations described below on the open file descriptor fd.The operation is determined by cmd.
                    // int fcntl(int fd, int cmd, ... /* arg */ );
                    // Changing the capacity of a pipe F_SETPIPE_SZ (int; since Linux 2.6.35)
                    // Change the capacity of the pipe referred to by fd to be at least arg bytes.  An unprivileged process can adjust the pipe
                    // capacity to any value between the system page size and the limit defined in /proc/sys/fs/pipe-max-size (see proc(5)).
                    // Attempts by an unprivileged process to set the pipe capacity above the limit yield the error EPERM; a privileged process (CAP_SYS_RESOURCE) can override the limit.
                    // The kernel may use a capacity larger than arg...
                    child_sts = (int)fcntl(sp->m_outpipefd[0], F_SETPIPE_SZ, buf_size);
                    if (child_sts == (-1))
                    {
                        fprintf(stderr, "Error: fcntl(sp->m_outpipefd[0], F_SETPIPE_SZ, buf_size) failed in child process, errno = %d.\n", errno);
                    }
                }
            }

            if (is_stdout_pipe)
            {
				//Duplicate the file descriptor of the pipe.
                child_sts = dup2(sp->m_inpipefd[1], STDOUT_FILENO);
                if (child_sts == (-1))
                {
                    fprintf(stderr, "Error: dup2 STDOUT_FILENO failed, errno = %d.\n", errno); exit(-1);
                }

                //Close unused ends of pipe (Rotem).
                child_sts = close(sp->m_inpipefd[0]);
                if (child_sts == (-1))
                {
                    fprintf(stderr, "Error: close(sp->m_inpipefd[0]) failed in child process, errno = %d.\n", errno);
                }

                if (buf_size > 0)
                {
                    //Set pipe buffer size
                    child_sts = (int)fcntl(sp->m_inpipefd[1], F_SETPIPE_SZ, buf_size);
                    if (child_sts == (-1))
                    {
                        fprintf(stderr, "Error: fcntl(sp->m_inpipefd[1], F_SETPIPE_SZ, buf_size) failed in child process, errno = %d.\n", errno);
                    }
                }
            }

            // Keep stderr unchanged for debugging.
            //child_sts = dup2(sp->m_inpipefd[1], STDERR_FILENO);
            //if (child_sts == (-1))
            //{
            //    fprintf(stderr, "Error: dup2 STDOUT_FILENO failed, errno = %d.\n", errno); exit(-1);
            //}

            // Ask kernel to deliver SIGTERM in case the parent dies.
            // https://man7.org/linux/man-pages/man2/prctl.2.html
            // prctl() manipulates various aspects of the behavior of the calling thread or process...
            // On error, -1 is returned, and errno is set appropriately.
            child_sts = prctl(PR_SET_PDEATHSIG, SIGTERM);
            if (child_sts == (-1))
            {
                fprintf(stderr, "Error: prctl failed, errno = %d.\n", errno); exit(-1);
            }

            // Execute child process (first argument may be full executable path, the second is the process name).
            // https://linux.die.net/man/3/execl
            // The exec() family of functions replaces the current process image with a new process image.
            // ...
            // The const char *arg and subsequent ellipses in the execl(), execlp(), and execle() functions can be thought of as arg0, arg1, ..., argn.
            // Together they describe a list of one or more pointers to null - terminated strings that represent the argument list available to the executed program.The first argument, by convention, 
            // should point to the filename associated with the file being executed.The list of arguments must be terminated by a NULL pointer, and, 
            // since these are variadic functions, this pointer must be cast(char *) NULL.
            // The execv(), execvp(), and execvpe() functions provide an array of pointers to null - terminated strings that represent the argument list available to the new program.
            // The first argument, by convention, should point to the filename associated with the file being executed.The array of pointers must be terminated by a NULL pointer.
            // Return Value: The exec() functions only return if an error has occurred. The return value is -1, and errno is set to indicate the error.
            child_sts = execvp(cmd.c_str(), (char* const*)sp->m_args_list);
        
            // Nothing below this line should be executed by child process. 
            // If so, it means that the "execl function" wasn't successful.
            if (child_sts == (-1))
            {
                fprintf(stderr, "Error: execl failed, errno = %d.\n", errno);
            }

            fprintf(stderr, "Error: child process wasn't successful.\n");
            exit(1);
            //////////////////////////////////////////////////////////////////////////
        }

        // https://stackoverflow.com/questions/6171552/popen-simultaneous-read-and-write
        // The code below will be executed only by parent. You can write and read
        // from the child using pipefd descriptors, and you can send signals to 
        // the process using its pid by kill() function. If the child process will
        // exit unexpectedly, the parent process will obtain SIGCHLD signal that
        // can be handled (e.g. you can respawn the child process).

        // Close unused pipe ends
        if (is_stdin_pipe)
        {
			// Close the unused end of pipe
            // https://linux.die.net/man/2/close
            // close() closes a file descriptor, so that it no longer refers to any file and may be reused...
            // Return Value: close() returns zero on success. On error, -1 is returned, and errno is set appropriately.
            sts = close(sp->m_outpipefd[0]);

            if (sts == (-1))
            {
                fprintf(stderr, "Error: close(sp->m_outpipefd[0]) failed, errno = %d.\n", errno);
                delete sp;
                return nullptr; // Returning nullptr  indication for an error.
            }

            if (buf_size > 0)
            {
                //Set pipe buffer size
                sts = (int)fcntl(sp->m_outpipefd[1], F_SETPIPE_SZ, buf_size);
                if (sts == (-1))
                {
                    fprintf(stderr, "Error: fcntl(sp->m_outpipefd[1], F_SETPIPE_SZ, buf_size) failed in parent process, errno = %d.\n", errno);
                }
            }
        }

        if (is_stdout_pipe)
        {
			// Close the unused end of pipe
            sts = close(sp->m_inpipefd[1]);

            if (sts == (-1))
            {
                fprintf(stderr, "Error: close(sp->m_inpipefd[1]) failed, errno = %d.\n", errno);
                delete sp;
                return nullptr; // Returning nullptr  indication for an error.
            }

            if (buf_size > 0)
            {
                //Set pipe buffer size
                sts = (int)fcntl(sp->m_inpipefd[0], F_SETPIPE_SZ, buf_size);
                if (sts == (-1))
                {
                    fprintf(stderr, "Error: fcntl(sp->m_inpipefd[0], F_SETPIPE_SZ, buf_size) failed in parent process, errno = %d.\n", errno);
                }
            }
        }

		//In case everything went well, return a pointer to the created object.
        return sp;
    }

    // Close sdtin PIPE and stdout PIPE, wait for child process to end, and delete sp.
    static bool ClosePipeAndDeleteObj(CSubprocess *sp)
    {
        int sts;

        if (sp != nullptr)
        {
            int stat_loc = 0;

            if (sp->m_is_stdin_pipe)
            {
                sts = close(sp->m_outpipefd[1]);
                if (sts == (-1))
                {
                    fprintf(stderr, "Error: close(sp->m_outpipefd[1]) failed, errno = %d.\n", errno);
                }
            }

            if (sp->m_is_stdout_pipe)
            {
                sts = close(sp->m_inpipefd[0]);
                if (sts == (-1))
                {
                    fprintf(stderr, "Error: close(sp->m_inpipefd[0]) failed, errno = %d.\n", errno);
                }
            }

            // Remark: we don't need to kill FFmpeg process, killing the process looses the last encoded video frames.
            // Send SIGKILL signal to the child process
            // https://man7.org/linux/man-pages/man2/kill.2.html
            // The kill() system call can be used to send any signal to any process group or process...
            // On success (at least one signal was sent), zero is returned. On error, -1 is returned, and errno is set appropriately.
            //sts = kill(sp->m_pid, SIGKILL);
            //if (sts == (-1))
            //{
            //    fprintf(stderr, "Error: kill(sp->m_pid, SIGKILL) failed, errno = %d.\n", errno);
            //}

            // https://linux.die.net/man/3/waitpid
            // wait, waitpid - wait for a child process to stop or terminate...
            // stat_loc shall be 0 if and only if the status returned is from a terminated child process...
            // Return Value:
            // If wait() or waitpid() returns because the status of a child process is available,
            // these functions shall return a value equal to the process ID of the child process for which status is reported.
            // If wait() or waitpid() returns due to the delivery of a signal to the calling process, -1 shall be returned and errno set to[EINTR].
            // If waitpid() was invoked with WNOHANG set in options, it has at least one child process specified by pid for which status is not available, 
            // and status is not available for any process specified by pid, 0 is returned.Otherwise, (pid_t)-1 shall be returned, and errno set to indicate the error.
            sts = waitpid(sp->m_pid, &stat_loc, 0);

            if (sts == (-1))
            {
                fprintf(stderr, "Error: waitpid(sp->m_pid, &stat_loc, 0) sts = %d, stat_loc = %d, errno = %d.\n", sts, stat_loc, errno);
            }
            else if (stat_loc != 0)
            {
                fprintf(stderr, "Warning: waitpid(sp->m_pid, &stat_loc, 0) stat_loc = %d.\n", stat_loc);    // FFmpeg returned value other than zero.
            }

            delete sp;
        }

        return true;
    }

    // Write to stdin PIPE (no flush?)
    bool stdinWrite(const unsigned char *data_bytes, const unsigned int len)
    {
        ssize_t sts;

        // https://linux.die.net/man/2/write
        // ssize_t write(int fd, const void *buf, size_t count);
        // write() writes up to count bytes from the buffer pointed buf to the file referred to by the file descriptor fd.
        // The number of bytes written may be less than count if, for example, there is insufficient space...
        // Remark: the case of "Number of bytes written may be less than count" is not handled by the following implementation.
        sts = write(m_outpipefd[1], data_bytes, len);

        if (sts == (-1))
        {
            fprintf(stderr, "Error: write(m_outpipefd[1] failed, errno = %d.\n", errno);
            return false;
        }

        if ((unsigned int)sts != len)
        {
			//Never enters...
			//In case it does, the code should be modified: using a loop for writing all the data to the pipe.
            fprintf(stderr, "Oops... the number of bytes written is less than count.\n");
        }

        return true;
    }

    // Read from stdout PIPE
    bool stdoutRead(const unsigned int len, unsigned char *data_bytes)
    {
        ssize_t n_bytes_read;

        // The third argument of read, "count", is the "The maximum number of bytes to be read."
        // We must use a loop for reading exactly <len> bytes from the pipe.
        ssize_t remain_len = (ssize_t)len;
        unsigned char *data_bytes_ptr = data_bytes;

        // https://man7.org/linux/man-pages/man2/read.2.html
        // ssize_t read(int fd, void *buf, size_t count);
        // read() attempts to read up to count bytes from file descriptor fd into the buffer starting at buf.
        // RETURN VALUE:
        // On success, the number of bytes read is returned (zero indicates end of file), and the file position is advanced by this number.
        // It is not an error if this number is smaller than the number of bytes requested;
        // this may happen for example because fewer bytes are actually available right now (maybe because we were close to end - of - file, or because we are reading from a pipe, or from a terminal), 
        // or because read() was interrupted by a signal.See also NOTES.
        // On error, -1 is returned, and errno is set appropriately. In this case, it is left unspecified whether the file position (if any) changes.
        // Keep reading until finish reading <len> bytes from the PIPE.
        while (remain_len > 0)
        {
			//Try to read remain_len bytes from the PIPE (but may read less).
            n_bytes_read = read(m_inpipefd[0], data_bytes_ptr, remain_len);

            if (n_bytes_read == (-1))
            {
                fprintf(stderr, "Error: read(m_inpipefd[0] failed, errno = %d.\n", errno);
                return false;
            }
            else if (n_bytes_read == 0)
            {
                fprintf(stderr, "Error: read(m_inpipefd[0], data_bytes_ptr, remain_len) read zero bytes instead of %d.\nThat means that child process is no longer running.\n", (int)n_bytes_read);
                return false;
            }

            remain_len -= n_bytes_read;     // Subtract number of bytes read from remain_len.
            data_bytes_ptr += n_bytes_read; // Advance pointer by number of bytes read.
        }

        return true;
    }


    // Close stdin PIPE
    bool stdinClose()
    {
        int sts;

        if (m_is_stdin_pipe)
        {
            sts = close(m_outpipefd[1]);

            if (sts == (-1))
            {
                fprintf(stderr, "Error: close(sp->m_outpipefd[1]) failed, errno = %d.\n", errno);
            }

            m_is_stdin_pipe = false;
        }

        return true;
    }
};



// Read header of FLV packet and return FLV payload size
// After the header, the file is split into packets called "FLV tags",
// which have 15 - byte packet headers.
// The first four bytes denote the size of the previous packet / tag
// (including the header without the first field), and aid in seeking backward
// <buf> - Pointer to sketch buffer - must be large enough (width*height*3 assumed to be large enough), but there is no size checking.
// Return -1 in case of an error.
// Return flv_payload_size if success.
static int ReadFlvPacketHeader(CSubprocess *ffmpeg_process, unsigned char *buf)
{
    // Read size_of_previous_packet, packet_type, flv_payload_size, timestamp_lower, timestamp_upper, stream_id
    bool success = ffmpeg_process->stdoutRead(4 + 1 + 3 + 3 + 1 + 3, buf);

    if (!success)
    {
        fprintf(stderr, "Unsuccessful ffmpeg_process read from PIPE in ReadFlvPacketHeader\n");
        return -1;
    }

    //size_of_previous_packet = f.read(4)  # For first packet set to NULL(uint32 big - endian)
    //packet_type = f.read(1)  # For first packet set to AMF Metadata
    //flv_payload_size = f.read(3)  # For first packet set to AMF Metadata(uint24 big - endian)
    //timestamp_lower = f.read(3)  # For first packet set to NULL(uint24 big - endian)
    //timestamp_upper = f.read(1)  # Extension to create a uint32_be value(0)
    //stream_id = f.read(3)  # For first stream of same type set to NULL(uint24 big - endian)
    //if len(flv_payload_size) < 3: return 0  # End of file.

    int flv_payload_size = ((int)buf[5] << 16) + ((int)buf[6] << 8) + (int)buf[7];

    // Convert uint24 big - endian to integer value
    return flv_payload_size;
}


// FLV files start with a standard header (9 bytes).
// After the header comes the first payload, which contains irrelevant data.
// The function reads the header and the first payload data.
// <buf> - Pointer to sketch buffer - must be large enough (width*height*3 assumed to be large enough), but there is no size checking.
static bool ReadFlvFileHeaderAndFirstPayload(CSubprocess *ffmpeg_process, unsigned char *buf)
{
    // https ://en.wikipedia.org/wiki/Flash_Video
    // Read FLV signature, version, flag byte and 4 bytes "used to skip a newer expanded header".
    bool success = ffmpeg_process->stdoutRead(5+4, buf);

    if (!success)
    {
        fprintf(stderr, "Unsuccessful ffmpeg_process read from PIPE in ReadFlvFileHeaderAndFirstPayload\n");
        return false;
    }

    if (((char)buf[0] != 'F') || ((char)buf[1] != 'L') || ((char)buf[2] != 'V'))
    {
        // FLV file must start with "FLV" letters.
        fprintf(stderr, "Bad signature: FLV stream doesn't start with FLV letters\n");
        return false;
    }

    unsigned char version_byte = buf[3];

    if (version_byte != 1)
    {
        // Version byte must be 1
        fprintf(stderr, "Bad version: FLV version is not 1\n");
        return false;
    }

    unsigned char flags_byte = buf[4];

    if (flags_byte != 1)
    {
        fprintf(stderr, "Bad flag byte: flags_byte = %d ... Bitmask: 0x04 is audio, 0x01 is video (so 0x05 is audio+video), but we expect video only\n", (int)flags_byte);
        return false;
    }
    
    // Read first packet(and ignore it).
    int flv_payload_size = ReadFlvPacketHeader(ffmpeg_process, buf);

    if (flv_payload_size < 0)
    {
        fprintf(stderr, "Error: ReadFlvPacketHeader (in ReadFlvFileHeaderAndFirstPayload) returned negative value (marking an error)\n");
        return false;
    }

    // Read "frame_type and _codec_id" byte and "AVC packet type" and payload data.
    success = ffmpeg_process->stdoutRead(flv_payload_size, buf);

    if (!success)
    {
        fprintf(stderr, "Unsuccessful ffmpeg_process read from PIPE in ReadFlvFileHeaderAndFirstPayload\n");
        return false;
    }
    
    int codec_id = (int)buf[0] & 0xF;
    //int frame_type = (int)buf[0] >> 4;  // 1 - keyframe, 2 - inter frame

    if (codec_id != 7)
    {
        fprintf(stderr, "Bad codec ID: Codec ID is not AVC. codec_id = %d, instead of 7\n", (int)codec_id);
        return false;
    }

    // Read AVC packet type for testing
    //int avc_packet_type = (int)buf[1]  // 0 - AVC sequence header, 1 - AVC NALU, 2 - AVC end of sequence

    // Ignore the data of the first payload (the first payload is just "meta data").

    return true;
}


// Read the 5 bytes of FLV packet header, and return codec_id
// https://www.adobe.com/content/dam/acom/en/devnet/flv/video_file_format_spec_v10.pdf
// <buf> - Pointer to sketch buffer - must be large enough (width*height*3 assumed to be large enough), but there is no size checking.
// <annexb_payload_buf> - Pointer to output buffer (Annex B payload) - must be large enough (width*height*3 assumed to be large enough), but there is no size checking.
// Return -1 in case of an error.
// Return Codec ID if success.
static int ReadPacket5BytesHeader(CSubprocess *ffmpeg_process, unsigned char *buf)
{
    // Read "frame_type and _codec_id" byte and avc_packet_type and composition_time.
    // According to video_file_format_spec_v10.pdf, if codec_id = 7, next comes AVCVIDEOPACKET
    bool success = ffmpeg_process->stdoutRead(5, buf);

    if (!success)
    {
        fprintf(stderr, "Unsuccessful ffmpeg_process read from PIPE in ReadPacket5BytesHeader\n");
        return -1;
    }

    int codec_id = (int)buf[0] & 0xF;
    //int frame_type = (int)buf[0] >> 4;  // 1 - keyframe, 2 - inter frame

    if (codec_id != 7)
    {
        fprintf(stderr, "Bad codec ID: Codec ID is not AVC. codec_id = %d, instead of 7\n", (int)codec_id);
        return -1;
    }
    
    unsigned char avc_packet_type = buf[1]; //# 0 - AVC sequence header, 1 - AVC NALU, 2 - AVC end of sequence

    if (avc_packet_type != 1)
    {
        fprintf(stderr, "Bad packet type: avc_packet_type = %d instead of 1\n", (int)codec_id);
        return -1;
    }

    return codec_id;
}


// Read FLV payload, and convert it to AVC Annex B format.
// Return Annex B data as bytes array(return None if end of file).
// The FLV payload may contain several AVC NAL units(in AVCC format).
// https://yumichan.net/video-processing/video-compression/introduction-to-h264-nal-unit/ """
// <buf> - Pointer to sketch buffer - must be large enough (width*height*3 assumed to be large enough), but there is no size checking.
// <annexb_payload_buf> - Pointer to output buffer (Annex B payload) - must be large enough (width*height*3 assumed to be large enough), but there is no size checking.
// Return -1 in case of an error.
// Return Annex B payload size if success.
static int ReadFlvPayloadAndConvertToAnnexB(CSubprocess *ffmpeg_process, unsigned char *buf, unsigned char *annexb_payload_buf)
{
    int annexb_payload_idx = 0; // Index in annexb_payload_buf

    // Read first packet(and ignore it).
    int flv_payload_size = ReadFlvPacketHeader(ffmpeg_process, buf);  

    if (flv_payload_size < 0)
    {
        fprintf(stderr, "Error: ReadFlvPacketHeader returned negative value (marking an error)\n");
        return -1;
    }

    int codec_id = ReadPacket5BytesHeader(ffmpeg_process, buf);

    if (codec_id < 0)
    {
        fprintf(stderr, "Error: ReadPacket5BytesHeader failed\n");
        return -1;
    }

    flv_payload_size -= 5;  // After reading the "5 Bytes Header", remaining size is 5 bytes less

    // Keep reading AVC NAL units, until flv_payload_size is zero (flv_payload_size holds the remaining size).
    while (flv_payload_size > 0)
    {
        bool success = ffmpeg_process->stdoutRead(4, buf);  // Read NAL unit size(uin32, big endian).

        if (!success)
        {
            fprintf(stderr, "Unsuccessful ffmpeg_process read from PIPE in ReadFlvPayloadAndConvertToAnnexB\n");
            return -1;
        }

        flv_payload_size -= 4;  // Remaining size is 4 bytes less


        // Convert uint32 big - endian to integer value
        int nal_size = ((int)buf[0] << 24) + ((int)buf[1] << 16) + ((int)buf[2] << 8) + (int)buf[3];

        success = ffmpeg_process->stdoutRead(nal_size, buf);    //Read NAL unit data

        if (!success)
        {
            fprintf(stderr, "Unsuccessful ffmpeg_process read from PIPE in ReadFlvPayloadAndConvertToAnnexB\n");
            return -1;
        }

        flv_payload_size -= nal_size;  // Remaining size is nal_size bytes less

        // The number of leading zeros(2 or 3) has minor differences between encoders(the implementation tries to match the selected encoder).
        // if do_use_intel_quick_sync if ((nal_data[0] & 0xF) == 5) or (nal_data[0] & 0xF == 1)... See Python code sample...       
        if (((buf[0] & 0xF) == 5) || ((buf[0] & 0xF) == 6))
        {
            // Coded slice of an IDR picture(for some reason begins with only 2 zeros when encoding with libx264)
            // SEI NAL unit(nal_data[0] == 6) is also begin with only 2 zeros.
            // annexb_payload_head = b'\x00\x00\x01'  # Concatenate[0, 0, 1] to end of annexb_payload, and then NAL data
            annexb_payload_buf[annexb_payload_idx]      = 0;
            annexb_payload_buf[annexb_payload_idx+1]    = 0;
            annexb_payload_buf[annexb_payload_idx+2]    = 1;
            annexb_payload_idx += 3;    // Advance index by 3 (3 bytes were inserted).
        }
        else
        {
            // Other NAL units begins with 0 0 0 0 1 (for matching FFmpeg Annex B encoded stream)
            // annexb_payload_head = b'\x00\x00\x00\x01'  # Concatenate[0, 0, 0, 1] to end of annexb_payload, and then NAL data
            annexb_payload_buf[annexb_payload_idx]      = 0;
            annexb_payload_buf[annexb_payload_idx + 1]  = 0;
            annexb_payload_buf[annexb_payload_idx + 2]  = 0;
            annexb_payload_buf[annexb_payload_idx + 3]  = 1;
            annexb_payload_idx += 4;    // Advance index by 4 (4 bytes were inserted).
        }

        // Concatenate NAL data in Annex B format to annexb_payload
        // Copy NAL unit data from buf to annexb_payload_buf (not most efficient solution, but probably negligible).
        memcpy(&annexb_payload_buf[annexb_payload_idx], buf, nal_size);
        annexb_payload_idx += nal_size; // Advance index by nal_size bytes.
    }

    // The value of annexb_payload_idx equals the number of bytes copied to annexb_payload_buf.
    return annexb_payload_idx;
}

int main()
{
    fprintf(stderr, "Start execution...\n");

    //100 frames, resolution 1280x720, and 25 fps
    const int width = 1280;
    const int height = 720;
    const int n_frames = 100;
    const int fps = 25;

    const int raw_image_size_in_bytes = width * height * 3;	// raw video frame size in bytes (3 bytes per pixel).

    bool success;
    int annexb_payload_len;

    FILE *out_f = nullptr;

    unsigned char *raw_img_bytes = new unsigned char[raw_image_size_in_bytes];
    unsigned char *flv_bytes = new unsigned char[raw_image_size_in_bytes];  // Allocate much larger buffer than necessary.
    unsigned char *annexb_payload_buf = new unsigned char[raw_image_size_in_bytes];  // Allocate much larger buffer than necessary.

    if ((raw_img_bytes == nullptr) || (flv_bytes == nullptr) || (annexb_payload_buf == nullptr))
    {
        ErrorExit("Memory allocation error???");
    }

#ifdef DO_TEST_ZERO_LATENCY
    const int n_frames_latency = 0; // Latency of zero frames

    // FFmpeg subprocess with input PIPE (raw BGR video frames) and output PIPE (H.264 encoded stream in FLV container).
    const std::string ffmpeg_arg =
        "-hide_banner -threads 1 -framerate " + std::to_string(fps) +
        " -video_size " + std::to_string(width) + "x" + std::to_string(height) +
        " -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 " +
        "-x264-params bframes=0:force-cfr=1:no-mbtree=1:sync-lookahead=0:sliced-threads=1:rc-lookahead=0 " +
        "-g 10 -pix_fmt yuv444p -crf 10 " +
        "-f flv -flvflags no_sequence_end+no_metadata+no_duration_filesize -bsf:v dump_extra -an -sn -dn pipe:";


    // FFmpeg subprocess with same arguments, but without FLV container, and save output to a file (instead of stdout PIPE) for testing.
    const std::string ffmpeg_test_arg =
        "-y -hide_banner -threads 1 -framerate " + std::to_string(fps) +
        " -video_size " + std::to_string(width) + "x" + std::to_string(height) +
        " -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 " +
        "-x264-params bframes=0:force-cfr=1:no-mbtree=1:sync-lookahead=0:sliced-threads=1:rc-lookahead=0 " +
        "-g 10 -pix_fmt yuv444p -crf 10 -f h264 -an -sn -dn out.264";
#else
    // Using the following setting results latency of many frames
    const int n_frames_latency = 26;  // The exact value was found by trial and error

    // FFmpeg subprocess with input PIPE (raw BGR video frames) and output PIPE (H.264 encoded stream in FLV container).
    const std::string ffmpeg_arg =
        "-hide_banner -threads 1 -framerate " + std::to_string(fps) +
        " -video_size " + std::to_string(width) + "x" + std::to_string(height) +
        " -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 " +
        "-g 25 -bf 3 -pix_fmt yuv444p -crf 10 " +
        "-f flv -flvflags no_sequence_end+no_metadata+no_duration_filesize -bsf:v dump_extra -an -sn -dn pipe:";


    // FFmpeg subprocess with same arguments, but without FLV container, and save output to a file (instead of stdout PIPE) for testing.
    const std::string ffmpeg_test_arg =
        "-y -hide_banner -threads 1 -framerate " + std::to_string(fps) +
        " -video_size " + std::to_string(width) + "x" + std::to_string(height) +
        " -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 " +
        "-g 25 -bf 3 -pix_fmt yuv444p -crf 10 -f h264 -an -sn -dn out.264";

#endif

    // Create subprocess with stdin PIPE and stdout PIPE (first argument may be full path like "/usr/bin/ffmpeg", the second is the process name).
    // FFmpeg Static Builds for Linux: https://johnvansickle.com/ffmpeg/
    // Set PIPE buffer size to 1MB (1MB is the [default] maximum buffer size of unprivileged process in Ubuntu 18.04 64 bit)
    CSubprocess *ffmpeg_process = CSubprocess::Popen("./ffmpeg", "ffmpeg", ffmpeg_arg, true, true, 1048576);

    if (ffmpeg_process == nullptr)
    {
        ErrorExit("CreateProcess ffmpeg_process");
    }

    // Create subprocess with stdin PIPE (used for testing).
    // Warning: output file name containing spaces in not supported by current implementation.
    CSubprocess *ffmpeg_test_process = CSubprocess::Popen("./ffmpeg", "ffmpeg", ffmpeg_test_arg, true, false, 1048576);

    if (ffmpeg_test_process == nullptr)
    {
        ErrorExit("CreateProcess ffmpeg_test_process");
    }

    bool was_broken_by_error = false;

    for (int i = 0; i < n_frames; i++)
    {
        MakeRawFrameAsBytes(width, height, i, raw_img_bytes);

        success = ffmpeg_process->stdinWrite(raw_img_bytes, raw_image_size_in_bytes);

        if (!success)
        {
            fprintf(stderr, "Unsuccessful ffmpeg_process write to PIPE\n");
            was_broken_by_error = true;
            break;
        }

        // For testing
        success = ffmpeg_test_process->stdinWrite(raw_img_bytes, raw_image_size_in_bytes);

        if (!success)
        {
            fprintf(stderr, "Unsuccessful ffmpeg_test_process write to PIPE\n");
            was_broken_by_error = true;
            break;
        }

        if (i == 0)
        {
            // Read FLV header that should be ignored.
            success = ReadFlvFileHeaderAndFirstPayload(ffmpeg_process, flv_bytes);

            if (!success)
            {
                fprintf(stderr, "ReadFlvFileHeaderAndFirstPayload failed\n");
                was_broken_by_error = true;
                break;
            }

            // Open output file (Annex B stream format)
			// out_avcc.264 file is used for testing - used for comparing the FLV converted output to out.264 (output of ffmpeg_test_process).
            out_f = fopen("out_avcc.264", "wb");

            if (out_f == nullptr)
            {
                fprintf(stderr, "Error: failed to open file out_avcc.264 for writing\n");
                was_broken_by_error = true;
                break;
            }
        }

        // Assume <n_frames_latency> frames latency
        // Note: the reading process is supposed to be in a separate thread (performed here for simplicity).
        if (i >= n_frames_latency)
        {
            // Read FLV payload data and convert the AVC NAL unit / units from AVCC format to Annex B format.
            annexb_payload_len = ReadFlvPayloadAndConvertToAnnexB(ffmpeg_process, flv_bytes, annexb_payload_buf);

            if (annexb_payload_len < 0)
            {
                fprintf(stderr, "ReadFlvPayloadAndConvertToAnnexB failed\n");
                was_broken_by_error = true;
                break;
            }

            // Write encoded frame to output file.
            // Note: "encoded frame" may contain few NAL units, but each FLV payload applies one "encoded frame" (one "access unit").
            fwrite(annexb_payload_buf, 1, annexb_payload_len, out_f);  // Write to file for testing.
        }
    }

    // Close the "test process" (close the FFmpeg process that writes to out.264 file - used as reference).
    //////////////////////////////////////////////////////////////////////////
    success = ffmpeg_test_process->stdinClose();

    if (!success)
    {
        ErrorExit("ffmpeg_test_process->stdinClose");
    }

    success = CSubprocess::ClosePipeAndDeleteObj(ffmpeg_test_process);

    if (!success)
    {
        ErrorExit("StdInWr CloseHandle");
    }
    //////////////////////////////////////////////////////////////////////////



    if (!was_broken_by_error)
    {
        // Closing stdin "pushes" all the remaining frames from the encoder to stdout (FFmpeg feature).
        success = ffmpeg_process->stdinClose();

        if (!success)
        {
            ErrorExit("ffmpeg_process->stdinClose");
        }

        // Handle the last(delayed) "encoded frames".
        for (int i = 0; i < n_frames_latency; i++)
        {
            // Read FLV payload data and convert the AVC NAL unit / units from AVCC format to Annex B format.
            annexb_payload_len = ReadFlvPayloadAndConvertToAnnexB(ffmpeg_process, flv_bytes, annexb_payload_buf);

            if (annexb_payload_len < 0)
            {
                fprintf(stderr, "ReadFlvPayloadAndConvertToAnnexB failed\n");
                was_broken_by_error = true;
                break;
            }

            fwrite(annexb_payload_buf, 1, annexb_payload_len, out_f);  // Write to file for testing.
        }
    }

    if (out_f != nullptr)
    {
        fclose(out_f);	// Close out_avcc.264 file. 
    }

    if (!was_broken_by_error)
    {
        // Read extra trailing 4 bytes (FFmpeg puts the 4 bytes as a footer [instead of a header of the next frame]).
        success = ffmpeg_process->stdoutRead(4, flv_bytes);

        if (!success)
        {
            fprintf(stderr, "Failed reading extra 4 footer bytes from sdtin???\n");
        }
    }

    delete[] raw_img_bytes;

    delete[] annexb_payload_buf;

	//Wait for FFmpeg child process to end, and delete ffmpeg_process object (cleanup).
    success = CSubprocess::ClosePipeAndDeleteObj(ffmpeg_process);
    
    if (!success)
    {
        ErrorExit("StdInWr CloseHandle");
    }

    fprintf(stderr, "Finish execution!\n");

    return 0;
}
