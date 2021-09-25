#include <filesystem>
#include <vector>
#include <thread>
#include <mutex>
#include <stack>
#include <memory>
#include <string>
#include "logic.h"
#include "ui.h"
#include <FL/Fl_File_Chooser.H>
#include <vendor/ncmdump-master/ncmcrypt.h>

typedef user_interface* p_ui;

namespace fs = std::filesystem;

namespace
{
	std::vector<fs::path> get_all_files_path_with_ext(const std::string& root_path, const std::string& ext);
	void convert_all(p_ui ui);
	void convert_some(p_ui ui,
	                  size_t ncms_size,
	                  std::stack<fs::path, std::vector<fs::path>>& stacked_ncms,
	                  std::vector<fs::path>& failed,
	                  std::vector<fs::path>& succeeded,
	                  uint32_t& count,
	                  std::mutex& mtx);

	template<typename V>
	class safe_pusher
	{
		typedef typename V::value_type E;
		bool succeeded_ = false;
		V &v_succ_;
		V &v_fail_;
		E &e_;
		std::mutex &mtx_;
	public:
		safe_pusher(V &vs, V &vf, E &e, std::mutex &mtx) : v_succ_(vs), v_fail_(vf), e_(e), mtx_(mtx) { mtx_.lock(); }
		~safe_pusher()
		{
			if (succeeded_)
				v_succ_.push_back(e_);
			else
				v_fail_.push_back(e_);
			mtx_.unlock();
		}
		void succeeded() { succeeded_ = true; }

		safe_pusher(const safe_pusher &) = delete;
		safe_pusher(safe_pusher &&) noexcept = delete;
		safe_pusher &operator=(const safe_pusher &) = delete;
		safe_pusher &operator=(safe_pusher &&) noexcept = delete;
	};
}

void btn_sel_path_cb(Fl_Widget* w, void* data)
{
	p_ui ui = static_cast<p_ui>(data);
	auto path = fl_dir_chooser("Choose a Directory", fs::current_path().root_path().string().c_str(), 0);
	ui->set_path(path);
}

void btn_stop_cb(Fl_Widget* w, void* data)
{
	p_ui ui = static_cast<p_ui>(data);
	ui->set_stop();
}

void btn_go_cb(Fl_Widget* w, void* data)
{
	p_ui ui = static_cast<p_ui>(data);
	// if there is a currently running converter thread, join it.
	if (ui->converter_thread && ui->converter_thread->joinable())
	{
		ui->converter_thread->join();
		delete ui->converter_thread;
		ui->converter_thread = nullptr;
	}

	// make the go button unclickable
	ui->deactivate_btn_go();

	// start a new thread for converting
	ui->converter_thread = new std::thread(convert_all, ui);
}

void text_disp_log_callback(Fl_Widget* w, void* data)
{
	/*p_ui ui = static_cast<p_ui>(data);
	ui->text_display_set_pos();*/
}


namespace
{
	std::vector<fs::path> get_all_files_path_with_ext(const std::string& root_path, const std::string& ext)
	{
		auto root = fs::path(root_path).make_preferred();
		std::vector<fs::path> v;
		v.reserve(300);
		for (auto& p : fs::recursive_directory_iterator(root))
		{
			if (ext == p.path().extension())
			{
				v.push_back(p.path());
			}
		}
		// copy elision
		return v;
	}

	void convert_all(p_ui ui)
	{
		ui->log_clear();

		auto root_path = fs::path(ui->get_path());

		if (!is_directory(root_path))
			// no need to add fs:: because of the special name lookup rule. same as "std::cout << something" with operator << not specifing std::.
		{
			ui->log_append("Error: Path must be a legal directory in which ncm files are located.\n");
			ui->activate_btn_go();
			return;
		}

		auto ncms = get_all_files_path_with_ext(ui->get_path(), ".ncm");

		if (ncms.empty())
		{
			ui->log_append("No ncm files found.\n");
			ui->activate_btn_go();
			return;
		}

		std::vector<fs::path> failed;
		std::vector<fs::path> succeeded;
		failed.reserve(ncms.size());
		succeeded.reserve(ncms.size());

		uint32_t count = 0;


		// start more threads to do the actual converting
		std::stack stacked_ncms(ncms);
		std::mutex mtx;
		constexpr size_t num_of_threads = 4;
		/*std::vector converter_threads(4, std::thread(convert_some,
		                                             std::ref(ui), ncms.size(),
		                                             std::ref(stacked_ncms), std::ref(failed),
		                                             std::ref(succeeded), std::ref(count), std::ref(mtx)
					                                 )
		);*/
		
		std::vector<std::thread> converter_threads;
		converter_threads.reserve(num_of_threads);
		for(size_t i = 0; i != num_of_threads; ++i)
		{
			converter_threads.emplace_back(
				convert_some,
				std::ref(ui), ncms.size(),
				std::ref(stacked_ncms), std::ref(failed),
				std::ref(succeeded), std::ref(count), std::ref(mtx)
			);
		}

		for (auto& thread : converter_threads)
		{
			if (thread.joinable())
				thread.join();
		}

		// if delete checkbox checked, and conversion wasn't interrupted by user, delete all ncm that has been successfully converted
		if (ui->get_delete() && !ui->get_stop())
		{
			std::error_code ec;
			for (auto& ncm : succeeded)
				fs::remove(ncm, ec);
			ui->log_append("Deleted all successfully converted ncm files .\n");
		}

		// output statistics info
		ui->log_append("==========\n");

		auto fin_msg = std::make_unique<char[]>(256);

		sprintf(fin_msg.get(), "%llu\tSucceeded\n%llu\tFailed\n", succeeded.size(), failed.size());
		ui->log_append(fin_msg.get());

		if (!failed.empty())
		{
			if (1 == failed.size())
				ui->log_append("Which is:\n");
			else
				ui->log_append("Which are:\n");

			for (auto& ncm : failed)
				ui->log_append((ncm.u8string() + u8'\n').c_str());
		}

		// make the go button clickable
		ui->clr_stop();
		ui->activate_btn_go();
	}

	void convert_some(p_ui ui,
	                  size_t ncms_size,
	                  std::stack<fs::path, std::vector<fs::path>>& stacked_ncms,
	                  std::vector<fs::path>& failed,
	                  std::vector<fs::path>& succeeded,
	                  uint32_t& count,
	                  std::mutex& mtx)
	{
		while (!stacked_ncms.empty())
		{
			// if stop button is clicked
			if (ui->get_stop())
				return;

			// pop 1 ncm out to convert
			fs::path ncm;
			uint32_t count_local = 0;
			{
				std::lock_guard lock(mtx);
				// check empty again after thread lock
				if (stacked_ncms.empty()) continue;

				ncm = std::move(stacked_ncms.top());
				stacked_ncms.pop();
				count++;
				// make a local copy of count for later use.
				// can't use count directly because it might be modified by other threads
				count_local = count;
			}

			auto log_line = std::make_unique<char[]>(256 + ncm.u8string().length());

			// behavior when ncm already converted
			fs::path temp(ncm);
			if (exists(temp.replace_extension(".mp3")) || exists(temp.replace_extension(".flac")))
			{
				if (ui->get_skip())
				{
					/*{
						std::lock_guard lock(mtx);
						succeeded.push_back(ncm);
					}*/

					safe_pusher sp(succeeded, failed, ncm, mtx);
					
					sprintf(log_line.get(), "Skipped(%u/%llu): %s\n", count_local, ncms_size, ncm.u8string().c_str());
					ui->set_progress_if_higher(count_local * 1.0f / ncms_size);
					ui->log_append(log_line.get());

					sp.succeeded();
					continue;
				}

				try
				{
					fs::remove(temp.replace_extension(".mp3"));
					fs::remove(temp.replace_extension(".flac"));
				}
				catch (const fs::filesystem_error& e)
				{
					std::cerr << e.what() << std::endl;
				}
			}


			// convert
			try
			{
				safe_pusher sp(succeeded, failed, ncm, mtx);

				NeteaseCrypt crypt(ncm);
				crypt.Dump();
				crypt.FixMetadata();

				sp.succeeded();

				sprintf(log_line.get(), "Converted(%u/%llu): %s\n", count_local, ncms_size, ncm.u8string().c_str());
			}
			catch (const std::invalid_argument& e)
			{
				sprintf(log_line.get(), "Failed(%u/%llu): %s\n", count_local, ncms_size, ncm.u8string().c_str());
				std::string err_msg = std::string("Exception: Invalid Argument: ") + e.what() + '\n';
				ui->log_append(err_msg.c_str());
			}
			catch (std::exception& e)
			{
				sprintf(log_line.get(), "Failed(%u/%llu): %s\n", count_local, ncms_size, ncm.u8string().c_str());
				std::string err_msg = std::string("Exception: ") + e.what() + '\n';
				std::cerr << err_msg << std::endl;
				ui->log_append(err_msg.c_str());
			}
			catch (...)
			{
				sprintf(log_line.get(), "Failed(%u/%llu): %s\n", count_local, ncms_size, ncm.u8string().c_str());
				ui->log_append("Exception: Unknown Exception\n");
			}

			ui->set_progress_if_higher(count_local * 1.0f / ncms_size);
			ui->log_append(log_line.get());
		}
	}
}
