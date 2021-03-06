/*
Copyright 2011 Frans van den Bergh. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this list of
      conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice, this list
      of conditions and the following disclaimer in the documentation and/or other materials
      provided with the distribution.

THIS SOFTWARE IS PROVIDED BY Frans van den Bergh ''AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Frans van den Bergh OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those of the
authors and should not be interpreted as representing official policies, either expressed
or implied, of the Council for Scientific and Industrial Research (CSIR).
*/
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include <string.h>

#include <tclap/CmdLine.h>

using std::string;
using std::stringstream;

#include "include/common_types.h"
#include "include/thresholding.h"
#include "include/gamma_lut.h"
#include "include/mtf_core.h"
#include "include/mtf_core_tbb_adaptor.h"
#include "include/mtf_renderer_annotate.h"
#include "include/mtf_renderer_profile.h"
#include "include/mtf_renderer_grid.h"
#include "include/mtf_renderer_print.h"
#include "include/mtf_renderer_stats.h"
#include "config.h"

void convert_8bit_input(cv::Mat& cvimg, bool gamma_correct=true) {

    cv::Mat newmat = cv::Mat(cvimg.rows, cvimg.cols, CV_16UC1);
    cv::MatIterator_<uint16_t> it16 = newmat.begin<uint16_t>();
    cv::MatConstIterator_<uchar> it = cvimg.begin<uchar>();
    cv::MatConstIterator_<uchar> it_end = cvimg.end<uchar>();
    
    // can we determine what the current gamma is ?
    if (gamma_correct) {
        Gamma gamma;
        for(; it != it_end; ) {
            *it16 = gamma.linearize_gamma(*it);
            it++;
            it16++;
        }
    } else {
        for(; it != it_end; ) {
            *it16 = (uint16_t)(*it);
            it++;
            it16++;
        }
    }
    
    cvimg = newmat;
}

//-----------------------------------------------------------------------------
void print_version_info(void) {
    printf("MTF mapper version %d.%d\n", mtfmapper_VERSION_MAJOR, mtfmapper_VERSION_MINOR);
}

//-----------------------------------------------------------------------------
int main(int argc, char** argv) {

    stringstream ss;
    ss << mtfmapper_VERSION_MAJOR << "." << mtfmapper_VERSION_MINOR;
    
    TCLAP::CmdLine cmd("Measure MTF50 values across edges of rectangular targets", ' ', ss.str());
    TCLAP::UnlabeledValueArg<std::string>  tc_in_name("<input_filename>", 
        "Input image file name (many extensions supported)", true, "input.png", "string", cmd
    );
    TCLAP::UnlabeledValueArg<std::string>  tc_wdir("<working_directory>", 
        "Working directory (for output files); \".\" is fine", true, ".", "string", cmd
    );
    TCLAP::SwitchArg tc_profile("p","profile","Generate MTF50 profile", cmd, true);
    TCLAP::SwitchArg tc_annotate("a","annotate","Annotate input image with MTF50 values", cmd, true);
    TCLAP::SwitchArg tc_surface("s","surface","Generate MTF50 surface plots", cmd, true);
    TCLAP::SwitchArg tc_linear("l","linear","Input image is linear 8-bit (default for 8-bit is assumed to be sRGB gamma corrected)", cmd, false);
    TCLAP::SwitchArg tc_print("r","raw","Print raw MTF50 values", cmd, false);
    TCLAP::ValueArg<double> tc_angle("g", "angle", "Angular filter [0,360)", false, 1000, "angle", cmd);
    TCLAP::ValueArg<double> tc_thresh("t", "threshold", "Dark object threshold (0,1)", false, 0.75, "threshold", cmd);
    
    cmd.parse(argc, argv);

    cv::Mat cvimg = cv::imread(tc_in_name.getValue(),-1);
    
    if (cvimg.type() == CV_8UC3 || cvimg.type() == CV_16UC3) {
        printf("colour input image detected; converting to grayscale using 0.299R + 0.587G + 0.114B\n");
        cv::Mat dest;
        cv::cvtColor(cvimg, dest, CV_RGB2GRAY);  // force to grayscale
        cvimg = dest;
    }
    
    if (cvimg.type() == CV_8UC1) {
        printf("8-bit input image, upconverting %s\n", tc_linear.getValue() ? "with linear scaling" : "with sRGB gamma correction");
        convert_8bit_input(cvimg, !tc_linear.getValue());        
    } else {
        printf("16-bit input image, no upconversion required\n");
    }
   
    assert(cvimg.type() == CV_16UC1);
    
    // process working directory
    std::string wdir(tc_wdir.getValue());
    if (wdir[wdir.length()-1]) {
        wdir = tc_wdir.getValue() + "/";
    }

	#ifdef _WIN32
	// on windows, mangle the '/' into a '\\'
	std::string wdm;
	for (size_t i=0; i < wdir.length(); i++) {
		if (wdir[i] == '/') {
			wdm.push_back('\\');
			wdm.push_back('\\');
		} else {
			wdm.push_back(wdir[i]);
		}
	}
	wdir = wdm;
	#endif

    cv::Mat masked_img;
    
    printf("Computing gradients ...\n");
    Gradient gradient(cvimg, false);
    
    printf("Thresholding image ...\n");
    int brad_S = 2*cvimg.cols/3;
    double brad_threshold = tc_thresh.getValue();
    bradley_adaptive_threshold(cvimg, masked_img, brad_threshold, brad_S);
    
    printf("Component labelling ...\n");
    Component_labeller::zap_borders(masked_img);    
    Component_labeller cl(masked_img, 60, false, 4000);
    
    if (cl.get_boundaries().size() == 0) {
        printf("No black objects found. Try a lower threshold value with the -t option.\n");
        return 0;
    }
    
    // now we can destroy the thresholded image
    masked_img = cv::Mat(1,1, CV_8UC1);
    
    Mtf_core mtf_core(cl, gradient, cvimg);
    
    Mtf_core_tbb_adaptor ca(&mtf_core);
    
    printf("Parallel MTF50 calculation\n");
    parallel_for(blocked_range<size_t>(size_t(0), mtf_core.num_objects()), ca); 
    
    
    // now render the computed MTF values
    if (tc_annotate.getValue()){
        Mtf_renderer_annotate annotate(cvimg, wdir + string("annotated.png"));
        annotate.render(mtf_core.get_blocks());
    }
    
    bool gnuplot_warning = true;
    bool few_edges_warned = false;
    
    if (tc_profile.getValue()) {
        if (mtf_core.get_blocks().size() < 10) {
            printf("Warning: fewer than 10 edges found, so MTF50 surfaces/profiles will not be generated. Are you using suitable input images?\n");
            few_edges_warned = true;
        } else {
            Mtf_renderer_profile profile(wdir, string("profile.txt"),string("profile_peak.txt"), cvimg);
            profile.render(mtf_core.get_blocks());
            gnuplot_warning = !profile.gnuplot_failed();
        }
    }

    if (tc_surface.getValue()) {
        if (mtf_core.get_blocks().size() < 10) {
            if (!few_edges_warned) {
                printf("Warning: fewer than 10 edges found, so MTF50 surfaces/profiles will not be generated. Are you using suitable input images?\n");
            }
        } else {
            Mtf_renderer_grid grid(wdir, string("grid.txt"),  cvimg);
            grid.set_gnuplot_warning(gnuplot_warning);
            grid.render(mtf_core.get_blocks());
        }
    }
    
    if (tc_print.getValue()) {
        Mtf_renderer_print printer(wdir + string("raw_mtf_values.txt"), tc_angle.getValue() != 1000, tc_angle.getValue()/180.0*M_PI);
        printer.render(mtf_core.get_blocks());
    }
    
    Mtf_renderer_stats stats;
    stats.render(mtf_core.get_blocks());
    
    return 0;
}
