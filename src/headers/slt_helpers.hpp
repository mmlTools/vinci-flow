#pragma once

#include <string>      
#include <vector>

class QComboBox;

namespace smart_lt {

struct AnimOption {
	const char *label; 
	const char *value; 
};

void populate_scene_combo(QComboBox *combo,
                          const std::string &boundSceneName);

inline const std::vector<AnimOption> AnimInOptions = {
	{"Fade In",               "animate__fadeIn"},
	{"Fade In Up",            "animate__fadeInUp"},
	{"Fade In Down",          "animate__fadeInDown"},
	{"Fade In Left",          "animate__fadeInLeft"},
	{"Fade In Right",         "animate__fadeInRight"},
	{"Fade In Up Big",        "animate__fadeInUpBig"},
	{"Fade In Down Big",      "animate__fadeInDownBig"},
	{"Fade In Left Big",      "animate__fadeInLeftBig"},
	{"Fade In Right Big",     "animate__fadeInRightBig"},
	{"Back In Up",            "animate__backInUp"},
	{"Back In Down",          "animate__backInDown"},
	{"Back In Left",          "animate__backInLeft"},
	{"Back In Right",         "animate__backInRight"},
	{"Bounce In",             "animate__bounceIn"},
	{"Bounce In Up",          "animate__bounceInUp"},
	{"Bounce In Down",        "animate__bounceInDown"},
	{"Bounce In Left",        "animate__bounceInLeft"},
	{"Bounce In Right",       "animate__bounceInRight"},
	{"Zoom In",               "animate__zoomIn"},
	{"Zoom In Up",            "animate__zoomInUp"},
	{"Zoom In Down",          "animate__zoomInDown"},
	{"Zoom In Left",          "animate__zoomInLeft"},
	{"Zoom In Right",         "animate__zoomInRight"},
	{"Slide In Up",           "animate__slideInUp"},
	{"Slide In Down",         "animate__slideInDown"},
	{"Slide In Left",         "animate__slideInLeft"},
	{"Slide In Right",        "animate__slideInRight"},
	{"Flip In X",             "animate__flipInX"},
	{"Flip In Y",             "animate__flipInY"},
	{"Light Speed In Right",  "animate__lightSpeedInRight"},
	{"Light Speed In Left",   "animate__lightSpeedInLeft"},
	{"Jack In The Box",       "animate__jackInTheBox"},
	{"Roll In",               "animate__rollIn"},
	{"Custom (CSS class)", "custom"},
};

inline const std::vector<AnimOption> AnimOutOptions = {
	{"Fade Out",              "animate__fadeOut"},
	{"Fade Out Up",           "animate__fadeOutUp"},
	{"Fade Out Down",         "animate__fadeOutDown"},
	{"Fade Out Left",         "animate__fadeOutLeft"},
	{"Fade Out Right",        "animate__fadeOutRight"},
	{"Fade Out Up Big",       "animate__fadeOutUpBig"},
	{"Fade Out Down Big",     "animate__fadeOutDownBig"},
	{"Fade Out Left Big",     "animate__fadeOutLeftBig"},
	{"Fade Out Right Big",    "animate__fadeOutRightBig"},
	{"Back Out Up",           "animate__backOutUp"},
	{"Back Out Down",         "animate__backOutDown"},
	{"Back Out Left",         "animate__backOutLeft"},
	{"Back Out Right",        "animate__backOutRight"},
	{"Bounce Out",            "animate__bounceOut"},
	{"Bounce Out Up",         "animate__bounceOutUp"},
	{"Bounce Out Down",       "animate__bounceOutDown"},
	{"Bounce Out Left",       "animate__bounceOutLeft"},
	{"Bounce Out Right",      "animate__bounceOutRight"},
	{"Zoom Out",              "animate__zoomOut"},
	{"Zoom Out Up",           "animate__zoomOutUp"},
	{"Zoom Out Down",         "animate__zoomOutDown"},
	{"Zoom Out Left",         "animate__zoomOutLeft"},
	{"Zoom Out Right",        "animate__zoomOutRight"},
	{"Slide Out Up",          "animate__slideOutUp"},
	{"Slide Out Down",        "animate__slideOutDown"},
	{"Slide Out Left",        "animate__slideOutLeft"},
	{"Slide Out Right",       "animate__slideOutRight"},
	{"Flip Out X",            "animate__flipOutX"},
	{"Flip Out Y",            "animate__flipOutY"},
	{"Light Speed Out Right", "animate__lightSpeedOutRight"},
	{"Light Speed Out Left",  "animate__lightSpeedOutLeft"},
	{"Roll Out",              "animate__rollOut"},
	{"Custom (CSS class)", "custom"},
};

}
