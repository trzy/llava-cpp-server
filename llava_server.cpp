/*
 * llava_server.cpp
 * Bart Trzynadlowski, 2023
 * 
 * Simple LLaVA server. Use /llava endpoint to submit images and prompts. Wraps llama.cpp, which
 * support LLaVA.
 * 
 * Sample usage:
 * 
 *      bin/llava-server -m ggml-model-q5_k.gguf --mmproj mmproj-model-f16.gguf --port 8080
 *
 * If running on macOS, ensure ggml-metal.metal is present in the same location as the llava-server
 * binary (i.e., the bin/ directory). You can find this file in llama.cpp/.
 */

#include "web_server.hpp"

#include "llama.cpp/examples/llava/clip.h"
#include "llama.cpp/examples/llava/llava-utils.h"
#include "llama.cpp/common/common.h"
#include "llama.cpp/llama.h"
#include "llama.cpp/common/stb_image.h"

#include <cstdio>
#include <cstdlib>
#include <queue>
#include <thread>
#include <tuple>
#include <vector>

static bool clip_image_load_from_memory(std::shared_ptr<uint8_t[]> image_buffer, size_t image_buffer_size, clip_image_u8 *img)
{
    int nx, ny, nc;
    auto data = stbi_load_from_memory(image_buffer.get(), image_buffer_size, &nx, &ny, &nc, 3);
    if (!data) 
    {
        fprintf(stderr, "%s: failed to load image\n", __func__);
        return false;
    }

    img->nx = nx;
    img->ny = ny;
    img->size = nx * ny * 3;
    img->data = new uint8_t[img->size]();
    memcpy(img->data, data, img->size);

    stbi_image_free(data);

    return true;
}

static void perform_inference(
    const llava_request &request,
    httplib::Response &web_response,
    gpt_params &params,
    clip_ctx *ctx_clip,
    llama_context *ctx_llama
)
{
    std::cout << "Processing request:" << std::endl
              << "  System prompt: " << request.system_prompt << std::endl
              << "  User prompt  : " << request.user_prompt << std::endl
              << "  Image        : " << request.image_buffer_size << " bytes" << std::endl
              << std::endl;

    // load and preprocess the image
    clip_image_u8 img;
    clip_image_f32 img_res;

    if (!clip_image_load_from_memory(request.image, request.image_buffer_size, &img))
    {
        web_response.set_content("{\"error\": true, \"description\": \"unable to load image\"}", "application/json");
        return;
    }

    if (!clip_image_preprocess(ctx_clip, &img, &img_res, /*pad2square =*/ true))
    {
        fprintf(stderr, "%s: unable to preprocess image\n", __func__);
        web_response.set_content("{\"error\": true, \"description\": \"unable to preprocess image\"}", "application/json");
        return;
    }

    int n_img_pos  = clip_n_patches(ctx_clip);
    int n_img_embd = clip_n_mmproj_embd(ctx_clip);

    float *image_embd = (float *) malloc(clip_embd_nbytes(ctx_clip));

    if (!image_embd) 
    {
        fprintf(stderr, "Unable to allocate memory for image embeddings\n");
        web_response.set_content("{\"error\": true, \"description\": \"unable to allocate memory for image embeddings\"}", "application/json");
        return;
    }

    const int64_t t_img_enc_start_us = ggml_time_us();
    if (!clip_image_encode(ctx_clip, params.n_threads, &img_res, image_embd))
    {
        fprintf(stderr, "Unable to encode image\n");
        web_response.set_content("{\"error\": true, \"description\": \"unable to encode image\"}", "application/json");
        return;
    }
    const int64_t t_img_enc_end_us = ggml_time_us();

    // make sure that the correct mmproj was used, i.e., compare apples to apples
    int n_llama_embd = llama_n_embd(llama_get_model(ctx_llama));
    if (n_img_embd != n_llama_embd)
    {
        printf("%s: embedding dim of the multimodal projector (%d) is not equal to that of LLaMA (%d). Make sure that you use the correct mmproj file.\n", __func__, n_img_embd, n_llama_embd);
        web_response.set_content("{\"error\": true, \"description\": \"multimodal projector embedding dimensions are not equal to LLaMA, which may indicate the wrong mmproj file is being used\"}", "application/json");
        free(image_embd);
        return;
    }

    // process the prompt
    // llava chat format is "<system_prompt>USER: <image_embeddings>\n<textual_prompt>\nASSISTANT:"

    int n_past = 0;

    const int max_tgt_len = params.n_predict < 0 ? 256 : params.n_predict;

    // Clear state
    llama_kv_cache_tokens_rm(ctx_llama, -1, -1);

    // GG: are we sure that the should be a trailing whitespace at the end of this string?
    std::string prompt = request.system_prompt + "\nUSER: ";
    eval_string(ctx_llama, prompt.c_str(), params.n_batch, &n_past);
    eval_image_embd(ctx_llama, image_embd, n_img_pos, params.n_batch, &n_past);
    eval_string(ctx_llama, request.user_prompt.c_str(), params.n_batch, &n_past);
    eval_string(ctx_llama, "\nASSISTANT:",        params.n_batch, &n_past);

    // generate the response

    printf("\n");
    std::string output;
    for (int i = 0; i < max_tgt_len; i++)
    {
        const char * tmp = sample(ctx_llama, params, &n_past);
        if (strcmp(tmp, "</s>") == 0) break;

        output += tmp;
        printf("%s", tmp);
        fflush(stdout);
    }
    
    web_response.set_content("{\"error\": false, \"content\": \"" + escape_json(output) + "\"}", "application/json");

    printf("\n");

    {
        const float t_img_enc_ms = (t_img_enc_end_us - t_img_enc_start_us) / 1000.0;
        printf("\n%s: image encoded in %8.2f ms by CLIP (%8.2f ms per image patch)\n", __func__, t_img_enc_ms, t_img_enc_ms / n_img_pos);
    }

    llama_print_timings(ctx_llama);

    free(image_embd);
}

static void show_additional_info(int /*argc*/, char **argv)
{
    printf("\n web server options:\n");
    printf("  --host HOST           host to serve on (default: localhost)\n");
    printf("  --port PORT           port to serve on (default: 8080)\n");
    printf("  --log-http            enable http logging\n");
    printf("\n");
    printf("\n example usage: %s -m <llava-v1.5-7b/ggml-model-q5_k.gguf> --mmproj <llava-v1.5-7b/mmproj-model-f16.gguf> --image <path/to/an/image.jpg> [--temp 0.1] [-p \"describe the image in detail.\"]\n", argv[0]);
    printf("  note: a lower temperature value like 0.1 is recommended for better quality.\n");
}

static bool parse_command_line(int argc, char **argv, gpt_params &params, std::string &hostname, int &port, bool &enable_http_logging)
{
    // Convert to vector of strings
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++)
    {
        args.emplace_back(argv[i]);
    }

    // First, handle our custom parameters and then remove them
    for (auto it = args.begin()++; it != args.end(); )
    {
        if (*it == "--host" || *it == "--port")
        {
            std::string arg = *it;
            it = args.erase(it);    // remove this element, point to next one
            if (it == args.end())
            {
                fprintf(stderr, "error: %s requires one argument.\n", arg.c_str());
                return true;
            }
            else
            {
                if (arg == "--host")
                {
                    hostname = *it;
                }
                else
                {
                    port = std::stoi(*it);
                }                
                it = args.erase(it);
            }
        }
        else if (*it == "--log-http")
        {
            enable_http_logging = true;
            it = args.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Construct new argc, argv with our custom arguments removed
    int new_argc = args.size();
    char **new_argv = new char *[new_argc];
    for (int i = 0; i < new_argc; i++)
    {
        new_argv[i] = new char[args[i].size() + 1];
        memcpy(new_argv[i], args[i].c_str(), args[i].size() + 1);
    }
    
    // Parse using llama.cpp parser
    bool success = gpt_params_parse(new_argc, new_argv, params);

    // Clean up
    for (int i = 0; i < new_argc; i++)
    {
        delete [] new_argv[i];
    }
    delete [] new_argv;

    return success;
}

int main(int argc, char ** argv)
{
    ggml_time_init();

    gpt_params params;

    std::string hostname = "localhost";
    int port = 8080;
    bool enable_http_logging = false;
    if (!parse_command_line(argc, argv, params, hostname, port, enable_http_logging))
    {
        show_additional_info(argc, argv);
        return 1;
    }

    if (params.mmproj.empty())
    {
        gpt_print_usage(argc, argv, params);
        show_additional_info(argc, argv);
        return 1;
    }

    const char * clip_path = params.mmproj.c_str();

    auto ctx_clip = clip_model_load(clip_path, /*verbosity=*/ 1);

    llama_backend_init(params.numa);

    llama_model_params model_params = llama_model_default_params();
    llama_model * model = llama_load_model_from_file(params.model.c_str(), model_params);
    if (model == NULL)
    {
        fprintf(stderr , "%s: error: unable to load model\n" , __func__);
        return 1;
    }

    llama_context_params ctx_params = llama_context_default_params();

    ctx_params.n_ctx           = params.n_ctx < 2048 ? 2048 : params.n_ctx; // we need a longer context size to process image embeddings
    ctx_params.n_threads       = params.n_threads;
    ctx_params.n_threads_batch = params.n_threads_batch == -1 ? params.n_threads : params.n_threads_batch;

    // create a fresh llama context each time to reset state
    llama_context * ctx_llama = llama_new_context_with_model(model, ctx_params);
    if (ctx_llama == NULL)
    {
        fprintf(stderr , "%s: error: failed to create the llama_context\n" , __func__);
        return 1;
    }

    // Serve forever
    std::mutex mtx;
    run_web_server(hostname, port, enable_http_logging,
        [&mtx, &params, ctx_clip, ctx_llama](const llava_request &request, httplib::Response &response)
        {
            std::unique_lock lock(mtx);
            perform_inference(request, response, params, ctx_clip, ctx_llama);
        }
    );

    return 0;
}
