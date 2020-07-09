// main_windows.cpp
// -----------------
// Stream raw video frames to FFmpeg stdin PIPE,
// FFmpeg encodes the video in H.264 codec and FLV(Flash Video) container format.
// The code reads the FLV encoded data from FFmpeg stdout PIPE,
// the AVC NAL units are extracted and converted from AVCC to Annex B format.
// The output is a file containing H.264 elementary stream(in Annex B format).
// Detailed documentation is included in main_linux.cpp file (the high level of Windows and Linux implementations is the same).

#include <string>
#include <math.h>
#include <stdint.h>
#include <tchar.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <strsafe.h>
#include <windows.h>


#include "opencv2/opencv.hpp"
#include "opencv2/highgui.hpp"

// https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output


//#define DO_TEST_ZERO_LATENCY  // Enable for testing FFmpeg with zero frames latency (raw frame in, encoded out...)
#undef DO_TEST_ZERO_LATENCY     // Allow latency of multiple frames (first encoded frame is ready after multiple input raw video frames enters).


#define DO_REDIRECT_STDERR_OF_CHILD_PROCESS_TO_STDOUT_OF_CURRENT_PROCESS    // Enable for testing
//#undef DO_REDIRECT_STDERR_OF_CHILD_PROCESS_TO_STDOUT_OF_CURRENT_PROCESS


// Build synthetic "raw BGR" image for testing, image data is stored in <raw_img_bytes> output data buffer.
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


// https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
// Format a readable error message, display a message box, and exit from the application.
void ErrorExit(const TCHAR *lpszFunction)
{
    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD dw = GetLastError();

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
        (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
    StringCchPrintf((LPTSTR)lpDisplayBuf,
        LocalSize(lpDisplayBuf) / sizeof(TCHAR),
        TEXT("%s failed with error %d: %s"),
        lpszFunction, dw, lpMsgBuf);
    MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    ExitProcess(1);
}


class CSubprocess
{
private:
    // The following members are global variables in Microsoft code sample.
    HANDLE m_hChildStd_IN_Rd    = nullptr;
    HANDLE m_hChildStd_IN_Wr    = nullptr;
    HANDLE m_hChildStd_OUT_Rd   = nullptr;
    HANDLE m_hChildStd_OUT_Wr   = nullptr;

    // The following members are not used in Microsoft code sample.
    wchar_t *m_cmd_as_wchar = nullptr;
    bool m_is_stdin_pipe    = false;
    bool m_is_stdout_pipe   = false;

    CSubprocess()
    {
    }

    ~CSubprocess()
    {
        if (m_cmd_as_wchar != nullptr)
        {
            delete[] m_cmd_as_wchar;
        }
    }

    // Create a child process that uses the previously created pipes for STDIN and STDOUT.
    bool createChildProcess()        
    {
        // TCHAR szCmdline[] = TEXT("child");
        PROCESS_INFORMATION piProcInfo;
        STARTUPINFO siStartInfo;
        BOOL bSuccess = FALSE;

        // Set up members of the PROCESS_INFORMATION structure. 

        ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

        // Set up members of the STARTUPINFO structure. 
        // This structure specifies the STDIN and STDOUT handles for redirection.

        ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
        siStartInfo.cb = sizeof(STARTUPINFO);
        siStartInfo.hStdError   = m_hChildStd_OUT_Wr;
        siStartInfo.hStdOutput  = m_hChildStd_OUT_Wr;
        siStartInfo.hStdInput   = m_hChildStd_IN_Rd;
        siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

        /*** Rotem ***/
#ifdef DO_REDIRECT_STDERR_OF_CHILD_PROCESS_TO_STDOUT_OF_CURRENT_PROCESS
        siStartInfo.hStdError   = GetStdHandle(STD_OUTPUT_HANDLE);  // Redirect stderr of child process to stdout of current (parent) process (for testing).
#endif
        /*** Rotem ***/

        // Create the child process. 
        bSuccess = CreateProcess(NULL,
                                 m_cmd_as_wchar,    // command line
                                 NULL,              // process security attributes 
                                 NULL,              // primary thread security attributes 
                                 TRUE,              // handles are inherited
                                 0,                 // creation flags 
                                 NULL,              // use parent's environment 
                                 NULL,              // use parent's current directory 
                                 &siStartInfo,      // STARTUPINFO pointer 
                                 &piProcInfo);      // receives PROCESS_INFORMATION 

        // If an error occurs, exit the application. 
        if (!bSuccess)
        {
            //ErrorExit(TEXT("CreateProcess"));
            return false;
        }
        else
        {
            // Close handles to the child process and its primary thread.
            // Some applications might keep these handles to monitor the status
            // of the child process, for example. 
            CloseHandle(piProcInfo.hProcess);
            CloseHandle(piProcInfo.hThread);

            // Close handles to the stdin and stdout pipes no longer needed by the child process.
            // If they are not explicitly closed, there is no way to recognize that the child process has ended.
            CloseHandle(m_hChildStd_OUT_Wr);
            CloseHandle(m_hChildStd_IN_Rd);

            return true;
        }
    }



public:
    // Executes child process with (optional) stdin and stdout pipes.
    // Return pointer to CSubprocess object in case of success, and nullptr in case of failure.
    // Remark: using std::wstring instead of std::string is not very useful here (cmd may contain unicode charcters).
    static CSubprocess *Popen(const std::wstring cmd, const bool is_stdin_pipe = false, const bool is_stdout_pipe = false, const int buf_size = 0)
    {
        CSubprocess *sp = new CSubprocess();

        sp->m_cmd_as_wchar = new wchar_t[cmd.length() + 1];
        wcscpy_s(sp->m_cmd_as_wchar, cmd.length() + 1, cmd.c_str());

        sp->m_is_stdin_pipe     = is_stdin_pipe;
        sp->m_is_stdout_pipe    = is_stdout_pipe;

        // Set the bInheritHandle flag so pipe handles are inherited. 
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength              = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle       = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        BOOL success;

        if (is_stdout_pipe)
        {
            // Create a pipe for the child process's STDOUT.
            success = CreatePipe(&sp->m_hChildStd_OUT_Rd, &sp->m_hChildStd_OUT_Wr, &saAttr, (DWORD)buf_size);

            if (!success)
            {
                // ErrorExit(TEXT("StdoutRd CreatePipe"));
                fprintf(stderr, "Error: StdoutRd CreatePipe\n");
                delete sp;
                return nullptr;
            }

            // Ensure the read handle to the pipe for STDOUT is not inherited.
            success = SetHandleInformation(sp->m_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);

            if (!success)
            {
                // ErrorExit(TEXT("StdoutRd CreatePipe"));
                fprintf(stderr, "Error: Stdout SetHandleInformation\n");
                delete sp;
                return nullptr;
            }
        }

        if (is_stdin_pipe)
        {
            // Create a pipe for the child process's STDIN.
            success = CreatePipe(&sp->m_hChildStd_IN_Rd, &sp->m_hChildStd_IN_Wr, &saAttr, (DWORD)buf_size);

            if (!success)
            {
                // ErrorExit(TEXT("Stdin CreatePipe"));
                fprintf(stderr, "Error: Stdin CreatePipe\n");
                delete sp;
                return nullptr;
            }
              
            // Ensure the write handle to the pipe for STDIN is not inherited.
            success = SetHandleInformation(sp->m_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0);

            if (!success)
            {
                // ErrorExit(TEXT("Stdin SetHandleInformation"));
                fprintf(stderr, "Error: Stdin SetHandleInformation\n");
                delete sp;
                return nullptr;
            }
        }

        // Create the child process.
        bool is_success = sp->createChildProcess();

        if (!is_success)
        {
            // If an error occurs, exit the application.
            delete sp;
            return nullptr;
        }
        
        return sp;
    }

    // Close sdtin PIPE and delete sp.
    static bool ClosePipeAndDeleteObj(CSubprocess *sp)
    {
        if (sp != nullptr)
        {
            if (sp->m_hChildStd_IN_Wr != nullptr)
            {
                BOOL success = CloseHandle(sp->m_hChildStd_IN_Wr);

                if (!success)
                {
                    return false;
                }
            }

            delete sp;
        }

        return true;
    }

    // Write to stdin PIPE and flush
    bool stdinWrite(const unsigned char *data_bytes, const unsigned int len)
    {
        DWORD dwWritten = 0;

        BOOL success = WriteFile(m_hChildStd_IN_Wr, (LPCVOID)data_bytes, (DWORD)len, &dwWritten, NULL);

        if (!success)
        {
            return false;
        }
        else
        {
            if (dwWritten != (DWORD)len)
            {
                fprintf(stderr, "len = %d, dwWritten = %d  Why???\n", len, (int)dwWritten);
            }            
        }

        success = FlushFileBuffers(m_hChildStd_IN_Wr);

        if (!success)
        {
            fprintf(stderr, "FlushFileBuffers failed\n");
            return false;
        }

        return true;
    }

    // Read from stdout PIPE
    bool stdoutRead(const unsigned int len, unsigned char *data_bytes)
    {
        DWORD dwRead = 0;

        // The third argument of ReadFile is nNumberOfBytesToRead is the "The maximum number of bytes to be read."
        // We must use a loop for reading exactly <len> bytes from the pipe.
        int remain_len = (int)len;
        unsigned char *data_bytes_ptr = data_bytes;
        
        // Keep reading until finish reading <len> bytes from the PIPE.
        while (remain_len > 0)
        {
            BOOL success = ReadFile(m_hChildStd_OUT_Rd, data_bytes_ptr, (DWORD)remain_len, &dwRead, NULL);

            if (!success)
            {
                return false;
            }

            remain_len -= (int)dwRead;  // Subtract number of bytes read from remain_len
            data_bytes_ptr += dwRead;   // Advance pointer by number of bytes read.
        }

        return true;
    }

    // Close stdin PIPE
    // Note: there is some code duplication from ClosePipeAndDeleteObj function (but ClosePipeAndDeleteObj was [kind of] taken from Microsoft code sample, and just kept)
    bool stdinClose()
    {
        if (m_hChildStd_IN_Wr != nullptr)
        {
            BOOL success = CloseHandle(m_hChildStd_IN_Wr);

            m_hChildStd_IN_Wr = nullptr;    // Mark as "closed", even if not success.

            if (!success)
            {
                return false;
            }
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
    
    unsigned char avc_packet_type = buf[1]; // 0 - AVC sequence header, 1 - AVC NALU, 2 - AVC end of sequence

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

        success = ffmpeg_process->stdoutRead(nal_size, buf);    // Read NAL unit data

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
    // 100 frames, resolution 1280x720, and 25 fps
    const int width = 1280;
    const int height = 720;
    const int n_frames = 100;
    const int fps = 25;

    const int raw_image_size_in_bytes = width * height * 3;

    bool success;
    int annexb_payload_len;

    FILE *out_f = nullptr;

    unsigned char *raw_img_bytes = new unsigned char[raw_image_size_in_bytes];
    unsigned char *flv_bytes = new unsigned char[raw_image_size_in_bytes];  // Allocate much larger buffer than necessary.
    unsigned char *annexb_payload_buf = new unsigned char[raw_image_size_in_bytes];  // Allocate much larger buffer than necessary.

    if ((raw_img_bytes == nullptr) || (flv_bytes == nullptr) || (annexb_payload_buf == nullptr))
    {
        ErrorExit(TEXT("Memory allocation error???"));
    }
   
#ifdef DO_TEST_ZERO_LATENCY
    const int n_frames_latency = 0; // Latency of zero frames

    // FFmpeg subprocess with input PIPE (raw BGR video frames) and output PIPE (H.264 encoded stream in FLV container).
    const std::wstring ffmpeg_cmd =
        L"ffmpeg.exe -hide_banner -threads 1 -framerate " + std::to_wstring(fps) +
        L" -video_size " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
        L" -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 " +
        L"-x264-params bframes=0:force-cfr=1:no-mbtree=1:sync-lookahead=0:sliced-threads=1:rc-lookahead=0 " +
        L"-g 10 -pix_fmt yuv444p -crf 10 " +
        L"-f flv -flvflags no_sequence_end+no_metadata+no_duration_filesize -bsf:v dump_extra -an -sn -dn pipe:";


    // FFmpeg subprocess with same arguments, but without FLV container, and save output to a file (instead of stdout PIPE) for testing.
    const std::wstring ffmpeg_test_cmd =
        L"ffmpeg.exe -y -hide_banner -threads 1 -framerate " + std::to_wstring(fps) +
        L" -video_size " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
        L" -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 " +
        L"-x264-params bframes=0:force-cfr=1:no-mbtree=1:sync-lookahead=0:sliced-threads=1:rc-lookahead=0 " +
        L"-g 10 -pix_fmt yuv444p -crf 10 -f h264 -an -sn -dn out.264";
#else
    // Using the following setting results latency of many frames
    const int n_frames_latency = 26;  // The exact value was found by trial and error
    
    // FFmpeg subprocess with input PIPE (raw BGR video frames) and output PIPE (H.264 encoded stream in FLV container).
    const std::wstring ffmpeg_cmd =
        L"ffmpeg.exe -hide_banner -threads 1 -framerate " + std::to_wstring(fps) +
        L" -video_size " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
        L" -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 " +
        L"-g 25 -bf 3 -pix_fmt yuv444p -crf 10 " +
        L"-f flv -flvflags no_sequence_end+no_metadata+no_duration_filesize -bsf:v dump_extra -an -sn -dn pipe:";


    // FFmpeg subprocess with same arguments, but without FLV container, and save output to a file (instead of stdout PIPE) for testing.
    const std::wstring ffmpeg_test_cmd =
        L"ffmpeg.exe -y -hide_banner -threads 1 -framerate " + std::to_wstring(fps) +
        L" -video_size " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
        L" -pixel_format bgr24 -f rawvideo -an -sn -dn -i pipe: -threads 1 -vcodec libx264 " +
        L"-g 25  -bf 3 -pix_fmt yuv444p -crf 10 -f h264 -an -sn -dn out.264";
#endif

    // Create subprocess with stdin PIPE and stdout PIPE
    CSubprocess *ffmpeg_process = CSubprocess::Popen(ffmpeg_cmd, true, true, raw_image_size_in_bytes);

    if (ffmpeg_process == nullptr)
    {
        ErrorExit(TEXT("CreateProcess ffmpeg_process"));
    }

    // Create subprocess with stdin PIPE (used for testing).
    CSubprocess *ffmpeg_test_process = CSubprocess::Popen(ffmpeg_test_cmd, true, false, raw_image_size_in_bytes);

    if (ffmpeg_test_process == nullptr)
    {
        ErrorExit(TEXT("CreateProcess ffmpeg_test_process"));
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

            // Open output file(Annex B stream format)
            fopen_s(&out_f, "out_avcc.264", "wb");

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

    // Close the "test process" (close the FFmpeg process that writes to out.264 file and used as reference).
    //////////////////////////////////////////////////////////////////////////
    success = ffmpeg_test_process->stdinClose();

    if (!success)
    {
        ErrorExit(TEXT("ffmpeg_test_process->stdinClose"));
    }

    success = CSubprocess::ClosePipeAndDeleteObj(ffmpeg_test_process);

    if (!success)
    {
        ErrorExit(TEXT("StdInWr CloseHandle"));
    }
    //////////////////////////////////////////////////////////////////////////



    if (!was_broken_by_error)
    {
        // Closing stdin "pushes" all the remaining frames from the encoder to stdout (FFmpeg feature).
        success = ffmpeg_process->stdinClose();

        if (!success)
        {
            ErrorExit(TEXT("ffmpeg_process->stdinClose"));
        }

        //Handle the last(delayed) "encoded frames".
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
        fclose(out_f);
    }

    if (!was_broken_by_error)
    {
        // Read extra trailing 4 bytes(FFmpeg puts the 4 bytes as a footer [instead of a header of the next frame]).
        success = ffmpeg_process->stdoutRead(4, flv_bytes);

        if (!success)
        {
            fprintf(stderr, "Failed reading extra 4 footer bytes from sdtin???\n");
        }
    }

    delete[] raw_img_bytes;
    delete[] flv_bytes;
    delete[] annexb_payload_buf;

    success = CSubprocess::ClosePipeAndDeleteObj(ffmpeg_process);

    if (!success)
    {
        ErrorExit(TEXT("StdInWr CloseHandle"));
    }

    return 0;
}
