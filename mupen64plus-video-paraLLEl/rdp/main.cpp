#include <algorithm>
#include <unordered_map>
#include <vector>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "common.hpp"
#include "frontend.hpp"
#include "rdp.hpp"
#include "rdp_dump.hpp"

#include "vulkan.hpp"
#include "vulkan_util.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

using namespace std;

static RDP::Frontend frontend;
static vector<uint8_t> RDRAM;

struct CLIParser;
struct CLICallbacks
{
	void add(const char *cli, const function<void(CLIParser &)> &func)
	{
		callbacks[cli] = func;
	}
	unordered_map<string, function<void(CLIParser &)>> callbacks;
	function<void()> error_handler;
	function<void(const char *)> default_handler;
};

struct CLIParser
{
	CLIParser(CLICallbacks cbs, int argc, char *argv[])
	    : cbs(move(cbs))
	    , argc(argc)
	    , argv(argv)
	{
	}

	bool parse()
	{
		try
		{
			while (argc && !ended_state)
			{
				const char *next = *argv++;
				argc--;

				if (*next != '-' && cbs.default_handler)
				{
					cbs.default_handler(next);
				}
				else
				{
					auto itr = cbs.callbacks.find(next);
					if (itr == ::end(cbs.callbacks))
					{
						throw logic_error("Invalid argument.\n");
					}

					itr->second(*this);
				}
			}

			return true;
		}
		catch (...)
		{
			if (cbs.error_handler)
			{
				cbs.error_handler();
			}
			return false;
		}
	}

	void end()
	{
		ended_state = true;
	}

	uint32_t next_uint()
	{
		if (!argc)
		{
			throw logic_error("Tried to parse uint, but nothing left in arguments.\n");
		}

		uint32_t val = stoul(*argv);
		if (val > numeric_limits<uint32_t>::max())
		{
			throw out_of_range("next_uint() out of range.\n");
		}

		argc--;
		argv++;

		return val;
	}

	double next_double()
	{
		if (!argc)
		{
			throw logic_error("Tried to parse double, but nothing left in arguments.\n");
		}

		double val = stod(*argv);

		argc--;
		argv++;

		return val;
	}

	const char *next_string()
	{
		if (!argc)
		{
			throw logic_error("Tried to parse string, but nothing left in arguments.\n");
		}

		const char *ret = *argv;
		argc--;
		argv++;
		return ret;
	}

	CLICallbacks cbs;
	int argc;
	char **argv;
	bool ended_state = false;
};

static const char *DP_command_names[64] = {
	"NOOP",         "reserved",          "reserved",     "reserved",    "reserved",     "reserved",    "reserved",
	"reserved",     "TRIFILL",           "TRIFILLZBUFF", "TRITXTR",     "TRITXTRZBUFF", "TRISHADE",    "TRISHADEZBUFF",
	"TRISHADETXTR", "TRISHADETXTRZBUFF", "reserved",     "reserved",    "reserved",     "reserved",    "reserved",
	"reserved",     "reserved",          "reserved",     "reserved",    "reserved",     "reserved",    "reserved",
	"reserved",     "reserved",          "reserved",     "reserved",    "reserved",     "reserved",    "reserved",
	"reserved",     "TEXRECT",           "TEXRECTFLIP",  "LOADSYNC",    "PIPESYNC",     "TILESYNC",    "FULLSYNC",
	"SETKEYGB",     "SETKEYR",           "SETCONVERT",   "SETSCISSOR",  "SETPRIMDEPTH", "SETRDPOTHER", "LOADTLUT",
	"reserved",     "SETTILESIZE",       "LOADBLOCK",    "LOADTILE",    "SETTILE",      "FILLRECT",    "SETFILLCOLOR",
	"SETFOGCOLOR",  "SETBLENDCOLOR",     "SETPRIMCOLOR", "SETENVCOLOR", "SETCOMBINE",   "SETTIMG",     "SETMIMG",
	"SETCIMG",
};

#define log(...)                          \
	do                                    \
	{                                     \
		if (args.verbose)                 \
			fprintf(stderr, __VA_ARGS__); \
	} while (0)

struct Image
{
	vector<uint8_t> buffer;
	unsigned width = 0;
	unsigned height = 0;
};

static void read_framebuffer(RDP::Renderer *renderer, Image *image)
{
	auto &fb = renderer->get_current_framebuffer();
	auto *buffer = RDRAM.data();

	image->buffer.resize(fb.allocated_width * fb.allocated_height * 4);

	auto ptr = image->buffer.data();
	image->width = fb.allocated_width;
	image->height = fb.allocated_height;
	unsigned pixels = image->width * image->height;

	if (fb.pixel_size == RDP::PIXEL_SIZE_32BPP)
	{
		for (unsigned i = 0; i < pixels; i++)
		{
			uint32_t pix = READ_DRAM_U32(buffer, fb.addr + 4 * i);
			*ptr++ = (pix >> 24) & 0xff;
			*ptr++ = (pix >> 16) & 0xff;
			*ptr++ = (pix >> 8) & 0xff;
			*ptr++ = 0xff;
		}
	}
	else if (fb.pixel_size == RDP::PIXEL_SIZE_16BPP)
	{
		for (unsigned i = 0; i < pixels; i++)
		{
			uint16_t pix = READ_DRAM_U16(buffer, fb.addr + 2 * i);
			uint8_t r = (pix >> 11) & 0x1f;
			uint8_t g = (pix >> 6) & 0x1f;
			uint8_t b = (pix >> 1) & 0x1f;
			*ptr++ = (r << 3) | (r >> 2);
			*ptr++ = (g << 3) | (g >> 2);
			*ptr++ = (b << 3) | (b >> 2);
			*ptr++ = 0xff;
		}
	}
	else if (fb.pixel_size == RDP::PIXEL_SIZE_8BPP)
	{
		for (unsigned i = 0; i < pixels; i++)
		{
			uint8_t pix = READ_DRAM_U8(buffer, fb.addr + 1 * i);
			*ptr++ = pix;
			*ptr++ = pix;
			*ptr++ = pix;
			*ptr++ = 0xff;
		}
	}
}

static bool read_png(const char *path, Image *image)
{
	int width, height, comp;
	auto *data = stbi_load(path, &width, &height, &comp, 4);
	if (!data)
		return false;

	image->width = width;
	image->height = height;
	image->buffer.clear();
	image->buffer.insert(end(image->buffer), data, data + width * height * 4);
	return true;
}

static void dump_framebuffer(RDP::Renderer *renderer, const char *path)
{
	Image image;
	read_framebuffer(renderer, &image);
	if (!stbi_write_png(path, image.width, image.height, 4, image.buffer.data(), image.width * 4))
		fprintf(stderr, "Failed to write image file: %s\n", path);
}

static void compare_framebuffer(RDP::Renderer *renderer, const char *path)
{
	Image ref_image;
	Image image;
	if (!read_png(path, &ref_image))
	{
		fprintf(stderr, "Failed to load file: %s\n", path);
		exit(1);
	}

	read_framebuffer(renderer, &image);

	if (image.width != ref_image.width || image.height != ref_image.height)
	{
		fprintf(stderr, "Dimension mismatch, Reference = %u x %u, Source = %u x %u.\n", ref_image.width,
		        ref_image.height, image.width, image.height);
		exit(1);
	}

	const uint8_t *ref = ref_image.buffer.data();
	const uint8_t *src = image.buffer.data();

	unsigned wrong = 0;
	unsigned correct = 0;
	for (unsigned y = 0; y < image.height; y++)
	{
		for (unsigned x = 0; x < image.width; x++)
		{
			// Ignore alpha.
			if (memcmp(ref, src, 3))
			{
				fprintf(stderr, "Difference at pixel (%03u, %03u).\n"
				                "  Reference = (%02x, %02x, %02x)\n"
				                "  Source    = (%02x, %02x, %02x)\n",
				        x, y, ref[0], ref[1], ref[2], src[0], src[1], src[2]);

				wrong++;
			}
			else
				correct++;

			ref += 4;
			src += 4;
		}
	}
	fprintf(stderr, "Correct pixels: %u, Wrong pixels: %u\n", correct, wrong);
}

struct CLIArguments
{
	const char *dump = nullptr;
	const char *output = nullptr;
	const char *compare = nullptr;
	bool verbose = false;
	bool video = false;

	unsigned trace_frame = 0;
	bool trace = false;
};

static void print_help()
{
	fprintf(stderr, "rdp-test [dump] [--output out png] [--compare compare against other png] [--verbose]\n");
}

static int rdp_dump_main(RDP::Renderer *renderer, Vulkan::Device *device, int argc, char *argv[])
{
	CLIArguments args;
	CLICallbacks cbs;

	cbs.add("--help", [](CLIParser &parser) {
		print_help();
		parser.end();
	});
	cbs.add("--output", [&args](CLIParser &parser) { args.output = parser.next_string(); });
	cbs.add("--video", [&args](CLIParser &) { args.video = true; });
	cbs.add("--compare", [&args](CLIParser &parser) { args.compare = parser.next_string(); });
	cbs.add("--verbose", [&args](CLIParser &) { args.verbose = true; });
	cbs.add("--trace", [&args](CLIParser &parser) {
		args.trace = true;
		args.trace_frame = parser.next_uint();
	});
	cbs.error_handler = [] { print_help(); };
	cbs.default_handler = [&args](const char *value) { args.dump = value; };

	CLIParser parser{ move(cbs), argc - 1, argv + 1 };
	if (!parser.parse())
		return 1;
	else if (parser.ended_state)
		return 0;

	if (!args.dump)
	{
		fprintf(stderr, "Didn't specify input file.\n");
		print_help();
		return 1;
	}

	RDP::Dump dump;
	if (!RDP::load_dump(args.dump, &dump))
		return 1;

	log("=== RDP DUMP ===\n");
	log("DRAM: %u\n", dump.dram_size);
	log("Command Lists: %u\n", unsigned(dump.lists.size()));

	RDRAM.resize(dump.dram_size);
	renderer->set_rdram(RDRAM.data(), RDRAM.size());

	if (args.trace || args.video)
		renderer->set_synchronous(true);

	unsigned begin_index = 0;
	double start_time = gettime();

	unsigned i = 0;
	for (auto &list : dump.lists)
	{
		log("  List #%u:\n", i);
		log("    DRAM updates: %u\n", unsigned(list.dram_updates.size()));
		log("\n");
		log("  Commands:\n");

		for (auto &update : list.dram_updates)
			memcpy(RDRAM.data() + update.offset, update.payload.data(), update.payload.size());

		unsigned draw_call_count = 0;
		bool trace = args.trace && i == args.trace_frame;
		frontend.reset_draw_count();

		// Mostly useful for breakpoints ...
		frontend.trace = trace;

		for (auto &cmd : list.commands)
		{
			log("%17s:", DP_command_names[cmd.command]);
			for (auto &arg : cmd.arguments)
				log(" 0x%x", arg);

			frontend.command(cmd.command, cmd.arguments.data());

			// Trace every draw call.
			if (trace && frontend.get_draw_count() != draw_call_count)
			{
				renderer->sync_full();
				device->begin_index(begin_index);
				renderer->begin_index(begin_index);

				char tmp[128];
				sprintf(tmp, "_%07u_%07u_color_0x%x_depth_0x%x_size_%u.png", i, frontend.get_draw_count(),
				        renderer->get_current_framebuffer().addr, renderer->get_current_framebuffer().depth_addr,
				        renderer->get_current_framebuffer().pixel_size);

				assert(args.output);
				string path = args.output;
				path += tmp;

				fprintf(stderr, "Tracing to: %s\n", path.c_str());
				dump_framebuffer(renderer, path.c_str());
				draw_call_count = frontend.get_draw_count();
			}

			log("\n");
		}
		log("\n\n");

		begin_index = (begin_index + 1) % 3;

		if (args.video)
		{
			renderer->sync_full();
			device->begin_index(begin_index);
			renderer->begin_index(begin_index);

			char tmp[128];
			sprintf(tmp, "_%07u_color_0x%x_depth_0x%x_size_%u.png", i, renderer->get_current_framebuffer().addr,
			        renderer->get_current_framebuffer().depth_addr, renderer->get_current_framebuffer().pixel_size);

			string path = args.output;
			path += tmp;

			fprintf(stderr, "Dumping to: %s\n", path.c_str());
			dump_framebuffer(renderer, path.c_str());
		}
		else
		{
			renderer->complete_frame();
			device->begin_index(begin_index);
			renderer->begin_index(begin_index);
		}

		renderer->get_vi_outputs().clear();
		i++;
	}

	renderer->sync_full();
	double end_time = gettime();

	fprintf(stderr, "Rendered %u lists in %.2f seconds (ms / frame: %.3f ms).\n", unsigned(dump.lists.size()),
	        end_time - start_time, 1000.0 * (end_time - start_time) / double(dump.lists.size()));

	if (args.output && !args.video)
	{
		dump_framebuffer(renderer, args.output);
	}

	if (args.compare)
	{
		renderer->sync_full();
		compare_framebuffer(renderer, args.compare);
	}

	fprintf(stderr, "Number of frames: %u\n", i);
	return 0;
}

int main(int argc, char *argv[])
{
	if (!Vulkan::VulkanContext::init_loader())
		return 1;

	auto vulkan_ctx = unique_ptr<Vulkan::VulkanContext>(new Vulkan::VulkanContext);
	Vulkan::Device vulkan_dev(*vulkan_ctx, 3);

	RDP::Renderer renderer(vulkan_dev);
	frontend.set_renderer(&renderer, &vulkan_dev);
	rdp_dump_main(&renderer, &vulkan_dev, argc, argv);
}
