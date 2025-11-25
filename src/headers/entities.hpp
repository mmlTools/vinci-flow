#pragma once

#include <string>
#include <cstdint>
#include <obs.h>

struct LowerThirdConfig {
	std::string id;              
	std::string title;           
	std::string subtitle;        

	std::string anim_in;         
	std::string anim_out;        

	std::string font_family;     
	std::string bg_color;        
	std::string text_color;      

	std::string html_template;   
	std::string css_template;    

	std::string hotkey;          
	std::string bound_scene;     
	bool visible = false;        

	std::string profile_picture; 

	obs_hotkey_id hotkey_id = OBS_INVALID_HOTKEY_ID; 
};

struct LowerThirdState {
	bool is_showing = false;
	std::uint64_t show_start_time = 0; 
};
