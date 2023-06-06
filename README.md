# E-Stim Audio Analyzer
Analyze e-stim audio files and create a video of the analysis.

[Example excerpt of video analysis from audio file](https://github.com/astoniab/estimanalyzer/raw/main/media/example1.mp4)

![Single frame from analysis video](https://github.com/astoniab/estimanalyzer/raw/main/media/example1.png)

## Example Usage
`estimanalyzer -i [inputfile] -o [outputfile]`

To see all available options run
`estimanalyzer -?`

Please note, depending on your computer specs, that it may take a while to analyze the input file and generate the output.  You should also have a few hundred MB of disk space free to allow for the creation of the output video.

## Input
Any audio file that can be read by sndfile can be used.  The analysis will work with an arbitrary number of audio channels.  In practice, 1 or 2 channels is common.  Analysis works best when audio files are used that consist of sine waves where all channels are the same frequency.

## Output
The audio will be analyzed and a series of video frames created.  ffmpeg is then used to combine the video frames and the original audio into a video file.

The output will consist of a column for each channel in the input file.

Under the column heading is the detected frequency for the input signal during that frame of video.

Under the frequency display is a circle that represents the intesity of the signal on this channel at this time.  The larger the circle the more intense the signal.

Under the intensity and between each channel is a horizontal line with another circle.  The size of the circle represents the difference between the channels above and roughly corresponds to how intense the difference is.  The position on the horizontal line roughly corresponds to where the signal will be felt.  Directly between the channels would be ground and directly under the channel would be the positive connection.

## Building
A CMakeLists.txt file is provided to compile with CMake.

Library Requirements:
- GD
- sndfile
- pffft - [This fork with expanded features](https://github.com/marton78/pffft)
