#include <iostream>
#include <vector>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <direct.h>

#include <sndfile.hh>
#include <gd.h>
#include <pffft.hpp>

#ifdef _WIN32
#include "dirent.h"
#endif

#define ESTIMANALYZERVERSION "0.0.2"

void fftframe(const int64_t channels, const std::vector<double> &data, std::vector<std::vector<std::complex<double>>> &channelfft)
{
	const int transformsize=pffft::Fft<double>::nearestTransformSize(data.size()/channels,false);
	channelfft.resize(channels);
	pffft::Fft<double> fft(transformsize);

	for(int64_t i=0; i<channels; i++)
	{
		pffft::AlignedVector<double> vv=fft.valueVector();
		pffft::AlignedVector<std::complex<double>> sv=fft.spectrumVector();

		for(int64_t s=0; s<vv.size(); s++)
		{
			vv[s]=data[(s*channels)+i];
		}

		fft.forward(vv,sv);

		channelfft[i].assign(sv.begin(),sv.end());
	}
}

void setupframesdirectory()
{
	_mkdir("frames");

	DIR *fd=opendir("frames");
	if(fd)
	{
		dirent *ent=readdir(fd);
		while(ent)
		{
			std::string fname(ent->d_name);

			if(fname.rfind(".png")==fname.size()-4)
			{
#ifdef _WIN32
				std::string relname("frames\\");
#else
				std::string relname("frames/");
#endif
				relname+=fname;
				_unlink(relname.c_str());
			}

			ent=readdir(fd);
		}
		closedir(fd);
	}

}

struct programoptions
{
	std::string filename;			// -i
	std::string ffmpeg;				// -ffmpeg
	int64_t videofps;				// -fps
	std::string outputfile;			// -o
	int64_t videowidth;				// -w
	int64_t videoheight;			// -h
	std::string fontname;			// -font
	bool printstatus;
	gdImagePtr bgimg;				// -bgimg
	gdImagePtr circ;
};

struct audiofilemetadata
{
	int64_t frequency;
	int64_t channels;
	int64_t minaudioframesperwindow;		// minimum number of audio frames in each window - some windows may need to +1 a to keep up with the frequency if not evenly divisible by videofps
};

struct audiochanneldata
{
	double prominentfrequency;						// based on FFT of samples in each channel
	double phaseshift;								// in radians for each channel
	double totalabsolutesampleenergy;				// total energy of all samples - absolute value of each sample added together
	double averageabsolutesampleenergy;				// totalabsolutesampleenergy/samplecount
	double channeltotaldiffabsolutesampleenergy;	// total energy of each sample abs(next channel sample - this channel sample) - wrap around on last channel to first channel
	double channelaveragediffabsolutesampleenergy;	
};

struct audiowindowdata
{
	int64_t framenumber;										// sequential index starting at 1
	int64_t samplecount;										// how many samples per channel were in this window
	std::vector<audiochanneldata> channeldata;

	void SetChannelCount(const int64_t channelcount)
	{
		channeldata.resize(channelcount);
	}

	double PhaseDiff(const int64_t c1, const int64_t c2) const
	{
		double diff=channeldata[c2].phaseshift-channeldata[c1].phaseshift;
		while(diff>M_PI)
		{
			diff-=(M_PI+M_PI);
		}
		while(diff<-M_PI)
		{
			diff+=(M_PI+M_PI);
		}
		return diff;
	}

};

void processaudioframe(const programoptions &opts, const audiofilemetadata &md, const int64_t framenumber, const std::vector<double> &samples, audiowindowdata &windowdata)
{
	std::vector<std::vector<std::complex<double>>> fftdata;

	windowdata.framenumber=framenumber;
	windowdata.samplecount=samples.size()/md.channels;
	windowdata.SetChannelCount(md.channels);

	fftframe(md.channels,samples,fftdata);

	for(int64_t c=0; c<md.channels; c++)
	{
		windowdata.channeldata[c].totalabsolutesampleenergy=0;
		windowdata.channeldata[c].channeltotaldiffabsolutesampleenergy=0;

		// find the FFT freq that best fits the samples in the frame
		{
			int64_t bestfftbucket=0;
			double bestfftfreqval=0;
			for(int j=0; j<fftdata[c].size(); j++)
			{
				double val=(fftdata[c][j].real()*fftdata[c][j].real())+(fftdata[c][j].imag()*fftdata[c][j].imag());
				if(val>bestfftfreqval)
				{
					bestfftbucket=j;
					bestfftfreqval=val;
				}
			}
			windowdata.channeldata[c].prominentfrequency=static_cast<double>(bestfftbucket)*(static_cast<double>(md.frequency)/static_cast<double>(fftdata[c].size())/2.0);
			windowdata.channeldata[c].phaseshift=atan2(fftdata[c][bestfftbucket].imag(),fftdata[c][bestfftbucket].real());
		}

		// total energy in this channel
		for(int64_t s=c; s<samples.size(); s+=md.channels)
		{
			windowdata.channeldata[c].totalabsolutesampleenergy+=fabs(samples[s]);
		}
		windowdata.channeldata[c].averageabsolutesampleenergy=windowdata.channeldata[c].totalabsolutesampleenergy/windowdata.samplecount;

		// next channel - this channel energy (wrap to first channel on last channel)
		for(int64_t s=0; s<windowdata.samplecount; s++)
		{
			windowdata.channeldata[c].channeltotaldiffabsolutesampleenergy+=fabs(samples[(s*md.channels)+((c+1)%md.channels)]-samples[(s*md.channels)+c]);
		}
		windowdata.channeldata[c].channelaveragediffabsolutesampleenergy=windowdata.channeldata[c].channeltotaldiffabsolutesampleenergy/windowdata.samplecount;

	}

}

int createwaveformfile(const programoptions &opts, const std::string &filename, const int64_t height)
{
	//http://trac.ffmpeg.org/wiki/FancyFilteringExamples
	//https://ffmpeg.org/ffmpeg-filters.html#showwaves

	std::ostringstream ffmpegcmd;

	ffmpegcmd << opts.ffmpeg << " -y -i \"" << opts.filename << "\" -lavfi showwavespic=split_channels=1:s=" << opts.videowidth << "x" << height << " \"" << filename << "\"";

	return system(ffmpegcmd.str().c_str());
}

int createvideofile(const programoptions &opts)
{
	//https://hamelot.io/visualization/using-ffmpeg-to-convert-a-set-of-images-into-a-video/

	std::ostringstream ffmpegcmd;

	std::string filename(opts.filename);
	std::string::size_type lastpos=filename.find_last_of("\\/");
	if(lastpos!=std::string::npos)
	{
		filename=filename.substr(lastpos+1);
	}

	ffmpegcmd << opts.ffmpeg << " -y -r " << opts.videofps << " -f image2 -s " << opts.videowidth << "x" << opts.videoheight << " -i frames";
#ifdef _WIN32
	ffmpegcmd << "\\";
#else
	ffmpegcmd << "/";
#endif
	ffmpegcmd << "frame_%08d.png -i \"" << opts.filename << "\" -vcodec libx264 -crf 25 -pix_fmt yuv420p -metadata title=\"" << filename << "\" -metadata comment=\"E-Stim Analyzer " << ESTIMANALYZERVERSION << "\" \"" << opts.outputfile << "\"";

	return system(ffmpegcmd.str().c_str());
}

void printtext(gdImagePtr im, const std::string &fontname, const int64_t size, std::string &text, const int64_t x, const int64_t y)
{
	gdImageStringFT(im,0,gdTrueColor(0,0,0),fontname.c_str(),size,0,x,y,text.c_str());
}

void drawantialiasedcircle(const programoptions &opts, gdImagePtr im, const int cx, const int cy, const int size)
{
	if(opts.circ)
	{
		gdImageCopyResampled(im,opts.circ,cx-(size/2),cy-(size/2),0,0,size,size,opts.circ->sx,opts.circ->sy);
	}
}

void writeimageoutput(const programoptions &opts, const audiofilemetadata &md, const std::vector<audiowindowdata> &windowdata)
{
	setupframesdirectory();

	_unlink("waveform.png");
	if(createwaveformfile(opts,"waveform.png",opts.videoheight-375)!=0)
	{
		std::cerr << "Error running ffmpeg.  Make sure the command to start ffmpeg is set correctly.  Current command:" << std::endl << opts.ffmpeg << std::endl;
		return;
	}
	gdImagePtr waveim=gdImageCreateFromFile("waveform.png");
	if(!waveim)
	{
		std::cerr << "Unable to create waveform image" << std::endl;
		return;
	}

	gdImagePtr im=gdImageCreateTrueColor(opts.videowidth,opts.videoheight);
	if(!im)
	{
		std::cerr << "Unable to create image (" << opts.videowidth << "," << opts.videoheight << ")" << std::endl;
		return;
	}

	const int64_t channeloutputwidth=opts.videowidth/md.channels;			// each channel will take up this much width
	const int64_t channeloutputxoffset=channeloutputwidth/2;				// the offset from the left of each channel for the center

	int white=gdTrueColor(255,255,255);
	int black=gdTrueColor(0,0,0);
	for(int64_t w=0; w<windowdata.size(); w++)
	{
		gdImageFilledRectangle(im,0,0,im->sx,im->sy,white);
		if(opts.bgimg)
		{
			gdImageCopy(im,opts.bgimg,0,0,0,0,std::min(opts.bgimg->sx,im->sx),std::min(opts.bgimg->sy,im->sy));
		}
		gdImageCopy(im,waveim,0,im->sy-waveim->sy,0,0,waveim->sx,waveim->sy);

		double currentxpos=static_cast<double>(w+1)/static_cast<double>(windowdata.size())*im->sx;
		gdImageLine(im,currentxpos,im->sy-waveim->sy,currentxpos,im->sy,black);

		for(int64_t c=0; c<md.channels; c++)
		{
			printtext(im,opts.fontname,16,"Channel "+std::to_string(c+1),channeloutputxoffset+(c*channeloutputwidth)-50,20);
			printtext(im,opts.fontname,10,std::to_string(static_cast<int64_t>(windowdata[w].channeldata[c].prominentfrequency))+" Hz",channeloutputxoffset+(c*channeloutputwidth)-25,35);

			int64_t cenergysize=windowdata[w].channeldata[c].averageabsolutesampleenergy*200;
			drawantialiasedcircle(opts,im,channeloutputxoffset+(c*channeloutputwidth),150,cenergysize);

			if(c<md.channels-1)
			{
				gdImageLine(im,channeloutputxoffset+(c*channeloutputwidth),300,channeloutputxoffset+(c*channeloutputwidth)+channeloutputwidth,300,black);
				int64_t denergysize=windowdata[w].channeldata[c].channelaveragediffabsolutesampleenergy*100;
				int64_t xoffset=(windowdata[w].PhaseDiff(c,(c+1)%md.channels)/M_PI)*(channeloutputwidth/2.0);
				drawantialiasedcircle(opts,im,(c+1)*channeloutputwidth+xoffset,300,denergysize);
			}

		}

		std::ostringstream ostr;
		ostr << "frames";
#ifdef _WIN32
		ostr << "\\";
#else
		ostr << "/";
#endif
		ostr << "frame_" << std::setfill('0') << std::setw(8) << windowdata[w].framenumber << ".png";
		gdImageFile(im,ostr.str().c_str());
	}

	if(im)
	{
		gdImageDestroy(im);
		im=nullptr;
	}
	if(waveim)
	{
		gdImageDestroy(waveim);
		waveim=nullptr;
	}

}

void processfile(const programoptions &opts)
{
	SndfileHandle snd(opts.filename,SFM_READ);
	
	if(snd.error()!=0)
	{
		std::cerr << "Error opening file " << opts.filename << std::endl;
		std::cerr << snd.strError() << std::endl;
		return;
	}

	audiofilemetadata md;
	md.frequency=snd.samplerate();
	md.channels=snd.channels();
	md.minaudioframesperwindow=std::floor(static_cast<double>(snd.samplerate())/static_cast<double>(opts.videofps));

	std::vector<audiowindowdata> windowdata;
	int64_t vf=1;		// video frame
	int64_t totalsf=0;	// total sample frames read so far
	bool done=false;

	if(opts.printstatus==true)
	{
		std::cout << "Processing input audio data" << std::endl;
	}

	while(done==false)
	{
		const int64_t totalexpectedframes=std::floor(static_cast<double>(md.frequency*vf)/static_cast<double>(opts.videofps));	// total number of audio frames up to and including this video frame
		int64_t getframes=totalexpectedframes-totalsf;
		
		while(totalsf+getframes < totalexpectedframes)
		{
			getframes++;
		}

		std::vector<double> buff(getframes*md.channels,0);
		int64_t gotframes=snd.readf(&buff[0],getframes);

		if(gotframes==0)
		{
			buff.clear();
			done=true;
		}
		else if(gotframes<getframes)
		{
			buff.resize(gotframes);
		}

		if(buff.size()>0)
		{
			audiowindowdata wd;
			processaudioframe(opts,md,vf,buff,wd);
			windowdata.push_back(wd);
		}

		totalsf+=gotframes;
		vf++;
	}

	if(opts.printstatus==true)
	{
		std::cout << "Writing output video frames" << std::endl;
	}

	writeimageoutput(opts,md,windowdata);

	if(opts.printstatus==true)
	{
		std::cout << "Creating video file" << std::endl;
	}

	createvideofile(opts);

	if(opts.printstatus==true)
	{
		std::cout << "Cleaning up temporary files" << std::endl;
	}

	setupframesdirectory();
	_unlink("waveform.png");

	if(opts.printstatus==true)
	{
		std::cout << "Done" << std::endl;
	}
}

void printoptions(const programoptions &opts)
{
	std::cout << "Version " << ESTIMANALYZERVERSION << std::endl << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << " -i [inputfile]            Path to input audio file (mp3 or wav)" << std::endl;
	std::cout << " -o [outputfile]           Path to output video file (mp4)" << std::endl;
	std::cout << " -ffmpeg [ffmpeg]          Command to run ffmpeg" << std::endl;
	std::cout << " -fps [fps]                Create video with this fps" << std::endl;
	std::cout << " -w [width]                Width of video" << std::endl;
	std::cout << " -h [height]               Height of video" << std::endl;
	std::cout << " -font [font]              Font file name to use" << std::endl;
	std::cout << " -bgimg [image]            Path to image file to use as video background" << std::endl;
}

void cleanupoptions(programoptions &opts)
{
	if(opts.bgimg)
	{
		gdImageDestroy(opts.bgimg);
		opts.bgimg=nullptr;
	}
	if(opts.circ)
	{
		gdImageDestroy(opts.circ);
		opts.circ=nullptr;
	}
}

int main(int argc, char *argv[])
{
	programoptions opts;

	opts.filename="";
	opts.fontname="tahomabd.ttf";
	opts.outputfile="out.mp4";
	opts.videofps=40;
	opts.videoheight=480;
	opts.videowidth=640;
#ifdef _WIN32
	opts.ffmpeg="ffmpeg\\ffmpeg.exe";
#else
	opts.ffmpeg="ffmpeg";
#endif
	opts.printstatus=true;
	opts.bgimg=nullptr;
	opts.circ=nullptr;

	for(int a=1; a<argc; )
	{
		std::string arg(argv[a]);
		if(arg=="-i" && a<argc && argv[a+1])
		{
			opts.filename=std::string(argv[a+1]);
			a+=2;
		}
		else if(arg=="-ffmpeg" && a<argc && argv[a+1])
		{
			opts.ffmpeg=std::string(argv[a+1]);
			a+=2;
		}
		else if(arg=="-fps" && a<argc && argv[a+1])
		{
			std::string fpsstr=std::string(argv[a+1]);
			std::istringstream istr(fpsstr);
			istr >> opts.videofps;
			a+=2;
		}
		else if(arg=="-o" && a<argc && argv[a+1])
		{
			opts.outputfile=std::string(argv[a+1]);
			a+=2;
		}
		else if(arg=="-w" && a<argc && argv[a+1])
		{
			std::string widthstr(argv[a+1]);
			std::istringstream istr(widthstr);
			istr >> opts.videowidth;
			a+=2;
		}
		else if(arg=="-h" && a<argc && argv[a+1])
		{
			std::string heightstr(argv[a+1]);
			std::istringstream istr(heightstr);
			istr >> opts.videoheight;
			a+=2;
		}
		else if(arg=="-f" && a<argc && argv[a+1])
		{
			opts.fontname=std::string(argv[a+1]);
			a+=2;
		}
		else if(arg=="-bgimg" && a<argc && argv[a+1])
		{
			opts.bgimg=gdImageCreateFromFile(argv[a+1]);
			if(!opts.bgimg || (opts.bgimg->sx<=0 || opts.bgimg->sy<=0))
			{
				std::cerr << "Unable to load background image" << std::endl;
				cleanupoptions(opts);
				return 1;
			}
			a+=2;
		}
		else if(arg=="-?")
		{
			printoptions(opts);
			cleanupoptions(opts);
			return 0;
		}
		else
		{
			std::cerr << "Unknown argument " << arg;
			cleanupoptions(opts);
			return 1;
		}
	}

	opts.circ=gdImageCreateTrueColor(401,401);
	if(opts.circ)
	{
		gdImageAlphaBlending(opts.circ,0);
		gdImageFilledRectangle(opts.circ,0,0,opts.circ->sx,opts.circ->sy,gdTrueColorAlpha(255,0,255,127));
		gdImageAlphaBlending(opts.circ,1);
		gdImageSaveAlpha(opts.circ,1);
		gdImageFilledEllipse(opts.circ,opts.circ->sx/2,opts.circ->sy/2,opts.circ->sx,opts.circ->sy,gdTrueColor(0,0,0));
	}

	processfile(opts);

	cleanupoptions(opts);

}
