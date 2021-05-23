/* Copyright 2021 Aristocratos (jakob@qvantnet.com)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

indent = tab
tab-size = 4
*/


#include <string>
#include <array>
#include <list>
#include <vector>
#include <thread>
#include <future>
#include <atomic>
#include <numeric>
#include <ranges>
#include <filesystem>
#include <unistd.h>

namespace Global {
	const std::vector<std::array<std::string, 2>> Banner_src = {
		{"#E62525", "██████╗ ████████╗ ██████╗ ██████╗"},
		{"#CD2121", "██╔══██╗╚══██╔══╝██╔═══██╗██╔══██╗   ██╗    ██╗"},
		{"#B31D1D", "██████╔╝   ██║   ██║   ██║██████╔╝ ██████╗██████╗"},
		{"#9A1919", "██╔══██╗   ██║   ██║   ██║██╔═══╝  ╚═██╔═╝╚═██╔═╝"},
		{"#801414", "██████╔╝   ██║   ╚██████╔╝██║        ╚═╝    ╚═╝"},
		{"#000000", "╚═════╝    ╚═╝    ╚═════╝ ╚═╝"},
	};

	const std::string Version = "0.0.10";
}

#include <btop_globs.h>
#include <btop_tools.h>
#include <btop_config.h>
#include <btop_input.h>
#include <btop_theme.h>
#include <btop_draw.h>

#if defined(__linux__)
	#define LINUX 1
	#include <btop_linux.h>
#elif defined(__unix__) || !defined(__APPLE__) && defined(__MACH__)
	#include <sys/param.h>
	#if defined(BSD)
		// #include <btop_bsd.h>
		#error BSD support not yet implemented!
	#endif
#elif defined(__APPLE__) && defined(__MACH__)
	#include <TargetConditionals.h>
	#if TARGET_OS_MAC == 1
		#define OSX 1
		// #include <btop_osx.h>
		#error OSX support not yet implemented!
    #endif
#else
	#error Platform not supported!
#endif

using std::string, std::vector, std::array, std::map, std::atomic, std::endl, std::cout, std::views::iota, std::list, std::accumulate;
using std::flush, std::endl, std::future, std::string_literals::operator""s, std::future_status;
namespace fs = std::filesystem;
using namespace Tools;


namespace Global {
	string banner;
	const uint banner_width = 49;

	fs::path conf_dir;
	fs::path conf_file;
	fs::path theme_folder;
	fs::path user_theme_folder;
}


//* A simple argument parser
void argumentParser(int argc, char **argv){
	string argument;
	for(int i = 1; i < argc; i++) {
		argument = argv[i];
		if (argument == "-v" || argument == "--version") {
			cout << "btop version: " << Global::Version << endl;
			exit(0);
		} else if (argument == "-h" || argument == "--help") {
			cout << "help here" << endl;
			exit(0);
		} else {
			cout << " Unknown argument: " << argument << "\n" <<
			" Use -h or --help for help." <<  endl;
			exit(1);
		}
	}
}

//* Generate the btop++ banner
string createBanner(){
	size_t z = 0;
	string b_color, bg, fg, out, oc, letter;
	bool truecolor = Config::getB("truecolor");
	int bg_i;
	for (auto line: Global::Banner_src) {
		fg = Theme::hex_to_color(line[0], !truecolor);
		bg_i = 120-z*12;
		bg = Theme::dec_to_color(bg_i, bg_i, bg_i, !truecolor);
		for (size_t i = 0; i < line[1].size(); i += 3) {
			if (line[1][i] == ' '){
				letter = ' ';
				i -= 2;
			} else{
				letter = line[1].substr(i, 3);
			}
			b_color = (letter == "█") ? fg : bg;
			if (b_color != oc) out += b_color;
			out += letter;
			oc = b_color;
		}
		if (++z < Global::Banner_src.size()) out += Mv::l(ulen(line[1])) + Mv::d(1);
	}
	out += Mv::r(18 - Global::Version.size()) + Fx::i + Theme::dec_to_color(0,0,0, !truecolor, "bg") +
			Theme::dec_to_color(150, 150, 150, !truecolor) + "v" + Global::Version + Fx::ui;
	return out;
}


//* Threading test function
string my_worker(int x){
	for (int i = 0; i < 100 + (x * 100); i++){
		sleep_ms(10);
		if (Global::stop_all.load()) return "Thread stopped! x=" + to_string(x);
	}
	return "Thread done! x=" + to_string(x);
}


//? --------------------------------------------- Main starts here! ---------------------------------------------------
int main(int argc, char **argv){

	//? Init

	cout.setf(std::ios::boolalpha);
	if (argc > 1) argumentParser(argc, argv);

	#if defined(LINUX)
		//? Linux init
		Global::proc_path = (fs::is_directory(fs::path("/proc")) && access("/proc", R_OK) != -1) ? "/proc" : "";
		if (Global::proc_path.empty()) {
			cout << "ERROR: Proc filesystem not found or no permission to read from it!" << endl;
			exit(1);
		}
	#endif

	//? Setup paths for config, log and themes
	for (auto env : {"XDG_CONFIG_HOME", "HOME"}) {
		if (getenv(env) != NULL && access(getenv(env), W_OK) != -1) {
			Global::conf_dir = fs::path(getenv(env)) / (((string)env == "HOME") ? ".config/btop" : "btop");
			break;
		}
	}
	if (!Global::conf_dir.empty()) {
		if (!fs::is_directory(Global::conf_dir) && !fs::create_directories(Global::conf_dir)) {
			cout << "WARNING: Could not create or access btop config directory. Logging and config saving disabled." << endl;
		}
		else {
			Global::conf_file = Global::conf_dir / "btop.conf";
			Logger::logfile = Global::conf_dir / "btop.log";
			Global::user_theme_folder = Global::conf_dir / "themes";
			if (!fs::exists(Global::user_theme_folder) && !fs::create_directory(Global::user_theme_folder)) Global::user_theme_folder.clear();
		}
	}
	for (auto theme_path : {"/usr/local/share/btop/themes", "/usr/share/btop/themes"}) {
		if (access(theme_path, R_OK) != -1) {
			Global::theme_folder = theme_path;
			break;
		}
	}

	string err_msg;

	if (!string(getenv("LANG")).ends_with("UTF-8") && !string(getenv("LANG")).ends_with("utf-8")) {
		err_msg = "No UTF-8 locale was detected! Symbols might not look as intended.";
		Logger::warning(err_msg);
		cout << "WARNING: " << err_msg << endl;
	}

	//? Initialize terminal and set options
	if (!Term::init()) {
		err_msg = "No tty detected!";
		Logger::error(err_msg + " Quitting.");
		cout << "ERROR: " << err_msg << endl;
		cout << "btop++ needs an interactive shell to run." << endl;
		exit(1);
	}

	//? Read config file if present
	Config::load("____");

	auto thts = time_ms();

	//? Generate the theme
	Theme::set(Theme::Default_theme);

	//? Create the btop++ banner
	Global::banner = createBanner();


	//* ------------------------------------------------ TESTING ------------------------------------------------------

	int debug = 1;
	int tests = 0;
	bool debuginit = false;

	if (debug > 0) { Logger::loglevel = 4; Logger::debug("Running in debug mode!");}

	// cout << Theme("main_bg") << Term::clear << flush;
	bool thread_test = false;

	if (!debuginit) cout << Term::alt_screen << Term::hide_cursor << flush;

	cout << Theme::c("main_fg") << Theme::c("main_bg") << Term::clear << endl;

	cout << Mv::r(Term::width / 2 - Global::banner_width / 2) << Global::banner << endl;
	// cout << string(Term::width - 1, '-') << endl;
	size_t blen = (Term::width > 200) ? 200 : Term::width;
	if (Term::width > 203) cout << Mv::r(Term::width / 2 - blen / 2) << flush;
	int ill = 0;
	for (int i : iota(0, (int)blen)){
		ill = (i <= (int)blen / 2) ? i : ill - 1;
		cout << Theme::g("used")[ill] << "-";
	}
	cout << Fx::reset << endl;

	//* Test theme
	if (false) {
		cout << "Theme generation took " << time_ms() - thts << "ms" << endl;

		cout << "Colors:" << endl;
		uint i = 0;
		for(auto& item : Theme::colors) {
			cout << rjust(item.first, 15) << ":" << item.second << "■"s * 10 << Fx::reset << "  ";
			// << Theme::dec(item.first)[0] << ":" << Theme::dec(item.first)[1] << ":" << Theme::dec(item.first)[2] << ;
			if (++i == 4) {
				i = 0;
				cout << endl;
			}
		}
		cout << Fx::reset << endl;


		cout << "Gradients:";
		for (auto& [name, cvec] : Theme::gradients) {
			cout << endl << rjust(name + ":", 10);
			for (auto& color : cvec) {
				cout << color << "■";
			}

			cout << Fx::reset << endl;
		}


		exit(0);
	}


	if (thread_test){

		map<int, future<string>> runners;
		map<int, string> outputs;

		for (int i : iota(0, 10)){
			runners[i] = async(my_worker, i);
		}
		// uint i = 0;
		while (outputs.size() < 10){

			for (int i : iota(0, 10)){
				if (runners[i].valid() && runners[i].wait_for(std::chrono::milliseconds(10)) == future_status::ready) {
					outputs[i] = runners[i].get();
					cout << "Thread " << i << " : " << outputs[i] << endl;
				}
			}

			// if (++i >= 10) i = 0;
			if (outputs.size() >= 8) Global::stop_all.store(true);
		}
	}



	cout << "Up for " << sec_to_dhms(round(system_uptime())) << endl;


//*------>>>>>> Proc testing


	auto timestamp = time_ms();
	Proc::init();



	uint lc;
	string ostring;
	uint64_t tsl, timestamp2, rcount = 0;
	list<uint64_t> avgtimes;
	uint timer = 1000;
	bool filtering = false;
	bool reversing = false;
	int sortint = Proc::sort_map["cpu lazy"];
	vector<string> greyscale;
	string filter;
	string filter_cur;
	string key;

	int xc;
	for (uint i : iota(0, (int)Term::height - 19)){
		xc = 230 - i * 150 / (Term::height - 20);
		greyscale.push_back(Theme::dec_to_color(xc, xc, xc));
	}

	string pbox = Box::draw(Box::Conf(0, 10, Term::width, Term::height - 16, Theme::c("proc_box"), "testbox", "below", true, 7));
	pbox += rjust("Pid:", 8) + " " + ljust("Program:", 16) + " " + ljust("Command:", Term::width - 69) + " Threads: " +
			ljust("User:", 10) + " " + rjust("MemB", 5) + " " + rjust("Cpu%", 14) + "\n";

	while (key != "q") {
		timestamp = time_ms();
		tsl = timestamp + timer;
		auto plist = Proc::collect(Proc::sort_array[sortint], reversing, filter);
		timestamp2 = time_ms();
		timestamp = timestamp2 - timestamp;
		ostring.clear();
		lc = 0;
		filter_cur = (filtering) ? Fx::bl + "█" + Fx::reset : "";
		ostring = Mv::save + Mv::u(2) + Mv::r(20) + trans(rjust("Filter: " + filter + filter_cur + string(Term::width / 3, ' ') +
			"Sorting: " + string(Proc::sort_array[sortint]), Term::width - 25, true, filtering)) + Mv::restore;

		for (auto& p : plist){
			ostring += 	Mv::r(1) + greyscale[lc] + rjust(to_string(p.pid), 8) + " " + ljust(p.name, 16) + " " + ljust(p.cmd, Term::width - 66, true) + " " +
						rjust(to_string(p.threads), 5) + " " + ljust(p.user, 10) + " " + rjust(floating_humanizer(p.mem, true), 5) + string(11, ' ');
			ostring += (p.cpu_p > 100) ? rjust(to_string(p.cpu_p), 3) + " " : rjust(to_string(p.cpu_p), 4);
			ostring += "\n";
			if (lc++ > Term::height - 21) break;
		}

		avgtimes.push_front(timestamp);
		if (avgtimes.size() > 100) avgtimes.pop_back();
		cout << pbox << ostring << Fx::reset << "\n" << endl;
		cout << Mv::to(Term::height - 4, 1) << "Processes call took: " << rjust(to_string(timestamp), 4) << "ms. Average: " << rjust(to_string(accumulate(avgtimes.begin(), avgtimes.end(), 0) / avgtimes.size()), 3) <<
			 "ms of " << avgtimes.size() << " samples. Drawing took: " << time_ms() - timestamp2 << "ms. Number of processes: " << Proc::numpids << ". Run count: " << ++rcount << ". Time: " << strf_time("%X   ") << endl;

		while (time_ms() < tsl) {
			if (Input::poll(tsl - time_ms())) key = Input::get();
			else { key.clear() ; continue; }
			if (filtering) {
				if (key == "enter") filtering = false;
				else if (key == "backspace") {if (!filter.empty()) filter = uresize(filter, ulen(filter) - 1);}
				else if (key == "space") filter.push_back(' ');
				else if (ulen(key) == 1 ) filter.append(key);
				else { key.clear(); continue; }
				break;
			}
			else if (key == "q") break;
			else if (key == "left") { if (--sortint < 0) sortint = (int)Proc::sort_array.size() - 1; }
			else if (key == "right") { if (++sortint > (int)Proc::sort_array.size() - 1) sortint = 0; }
			else if (key == "f") filtering = true;
			else if (key == "r") reversing = !reversing;
			else if (key == "delete") filter.clear();
			else continue;
			break;
		}
	}

	// cout << "Found " << plist.size() << " pids\n" << endl;

//*-----<<<<<

	//cout << pw->pw_name << "/" << gr->gr_name << endl;




	if (tests>4){
		string trim_test1 = "-*vad ";
		string trim_test2 = " vad*-";
		string trim_test3 = trim_test1 + trim_test2;

		cout << "\"" << ltrim(trim_test1, "-*") << "\" \"" << rtrim(trim_test2, "*-") << "\" \"" << trim(trim_test3, "-") << "\"" << endl;


		string testie = "Does this work as intended?    Or?";
		auto t_vec = ssplit(testie);
		for(auto& tp : t_vec){
			cout << "\"" << tp << "\" " << flush;
		}
	}


	//if (tests>5){

	//}

	// map<string, string> dict = {
	// 	{"Korv", "14"},
	// 	{"Vad", "13"}
	// };

	// cout << dict["Korv"] << ", " << dict["Vad"] << endl;

	// vector<map<string, int>> test = {
	// 	{{"first", 1}, {"second", 2}},
	// 	{{"first", 11}, {"second", 22}}
	// };

	//cout << test[0]["first"] << " " << test[1]["second"] << endl;

	// for (auto& m : test) {
	// 	cout << endl;
	// 	for (auto& item : m) {
	// 		cout << item.first << " " << item.second << endl;
	// 	}
	// }



	if (debug == 3){
		cout << Theme::c("main_fg");
		cout << Mv::to(Term::height - 1, 0) << "Press q to exit! Timeout" << flush;
		string full, key;
		int wt = 90;
		bool qp = false;
		while (!qp && wt >= 0){
			int wtm = wt / 60;
			int wts = wt - wtm * 60;
			wt--;
			cout << Mv::to(Term::height - 1, 26) << "(" << wtm << ":" << wts << ")    " << flush;
			//chr = Key(1000);
			if (Input::poll(1000)) {
				key = Input::get();
				cout << Mv::to(Term::height - 2, 1) << "Last key: LEN=" << key.size() << " ULEN=" << ulen(key) << " KEY=\"" << key << "\" CODE=" << (int)key.at(0) << "        " << flush;
				full += key;
				cout << Mv::to(Term::height - 5, 1) << full << flush;
				if (key == "q") qp = true;
				key = "";
				wt++;
			}
		}
	}

	Term::restore();
	if (!debuginit) cout << Term::normal_screen << Term::show_cursor << flush;
	return 0;
}
