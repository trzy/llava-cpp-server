#include "web_server.hpp"

#include "llama.cpp/examples/llava/clip.h"
#include "llama.cpp/examples/llava/llava-utils.h"
#include "llama.cpp/common/common.h"
#include "llama.cpp/llama.h"

//#define STB_IMAGE_IMPLEMENTATION
#include "llama.cpp/common/stb_image.h"

#include <cstdio>
#include <cstdlib>
#include <queue>
#include <thread>
#include <vector>

static std::queue<llava_request> s_work_queue;
static std::condition_variable s_cv;
static std::mutex s_mtx;

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

static void perform_inference(gpt_params params, llama_model *model, const llama_context_params &ctx_params, clip_ctx *ctx_clip, std::shared_ptr<uint8_t[]> image_buffer, size_t image_buffer_size, const std::string prompt)
{
    // create a fresh llama context each time to reset state
    llama_context * ctx_llama = llama_new_context_with_model(model, ctx_params);
    if (ctx_llama == NULL)
    {
        fprintf(stderr , "%s: error: failed to create the llama_context\n" , __func__);
        return;
    }

    // load and preprocess the image
    clip_image_u8 img;
    clip_image_f32 img_res;

    if (!clip_image_load_from_memory(image_buffer, image_buffer_size, &img))
    {
        return;
    }

    if (!clip_image_preprocess(ctx_clip, &img, &img_res, /*pad2square =*/ true))
    {
        fprintf(stderr, "%s: unable to preprocess image\n", __func__);
        return;
    }

    int n_img_pos  = clip_n_patches(ctx_clip);
    int n_img_embd = clip_n_mmproj_embd(ctx_clip);

    float *image_embd = (float *) malloc(clip_embd_nbytes(ctx_clip));

    if (!image_embd) 
    {
        fprintf(stderr, "Unable to allocate memory for image embeddings\n");
        return;
    }

    const int64_t t_img_enc_start_us = ggml_time_us();
    if (!clip_image_encode(ctx_clip, params.n_threads, &img_res, image_embd))
    {
        fprintf(stderr, "Unable to encode image\n");
        return;
    }
    const int64_t t_img_enc_end_us = ggml_time_us();

    // we get the embeddings, free up the memory required for CLIP
    //clip_free(ctx_clip);

    // make sure that the correct mmproj was used, i.e., compare apples to apples
    int n_llama_embd = llama_n_embd(llama_get_model(ctx_llama));
    if (n_img_embd != n_llama_embd)
    {
        printf("%s: embedding dim of the multimodal projector (%d) is not equal to that of LLaMA (%d). Make sure that you use the correct mmproj file.\n", __func__, n_img_embd, n_llama_embd);

        // Return error response
        // ...
        free(image_embd);
        return;
    }

    // process the prompt
    // llava chat format is "<system_prompt>USER: <image_embeddings>\n<textual_prompt>\nASSISTANT:"

    int n_past = 0;

    const int max_tgt_len = params.n_predict < 0 ? 256 : params.n_predict;

    // GG: are we sure that the should be a trailing whitespace at the end of this string?
    eval_string(ctx_llama, "A chat between a curious human and an artificial intelligence assistant.  The assistant gives helpful, detailed, and polite answers to the human's questions.\nUSER: ", params.n_batch, &n_past);
    eval_image_embd(ctx_llama, image_embd, n_img_pos, params.n_batch, &n_past);
    eval_string(ctx_llama, prompt.c_str(), params.n_batch, &n_past);
    eval_string(ctx_llama, "\nASSISTANT:",        params.n_batch, &n_past);

    // generate the response

    printf("\n");

    for (int i = 0; i < max_tgt_len; i++)
    {
        const char * tmp = sample(ctx_llama, params, &n_past);
        if (strcmp(tmp, "</s>") == 0) break;

        printf("%s", tmp);
        fflush(stdout);
    }

    printf("\n");

    {
        const float t_img_enc_ms = (t_img_enc_end_us - t_img_enc_start_us) / 1000.0;

        printf("\n%s: image encoded in %8.2f ms by CLIP (%8.2f ms per image patch)\n", __func__, t_img_enc_ms, t_img_enc_ms / n_img_pos);
    }

    llama_print_timings(ctx_llama);

    llama_free(ctx_llama);
    free(image_embd);
}

[[noreturn]] static void run_llava_thread(gpt_params params, llama_model *model, const llama_context_params &ctx_params, clip_ctx *ctx_clip)
{
    while (true)
    {
        // Wait until there is something in the queue
        std::unique_lock lock(s_mtx);
        s_cv.wait(lock, [] { return !s_work_queue.empty(); });
        auto request = s_work_queue.front();
        s_work_queue.pop();

        // Unlock, allowing more items to be enqueued
        lock.unlock();

        // Perform inference
        std::cout << "Prompt: " << request.prompt << std::endl;
        perform_inference(params, model, ctx_params, ctx_clip, request.image, request.image_buffer_size, request.prompt);
    }
}

static void push_to_work_queue(const llava_request &request)
{
    std::unique_lock lock(s_mtx);
    s_work_queue.emplace(request);
    s_cv.notify_all();
}

static void show_additional_info(int /*argc*/, char ** argv)
{
    printf("\n web server options:\n");
    printf("  --host HOST           host to serve on (default: localhost)\n");
    printf("  --port PORT           port to serve on (default: 8080)\n");
    printf("\n");
    printf("\n example usage: %s -m <llava-v1.5-7b/ggml-model-q5_k.gguf> --mmproj <llava-v1.5-7b/mmproj-model-f16.gguf> --image <path/to/an/image.jpg> [--temp 0.1] [-p \"describe the image in detail.\"]\n", argv[0]);
    printf("  note: a lower temperature value like 0.1 is recommended for better quality.\n");
}

int main(int argc, char ** argv)
{
    ggml_time_init();

    gpt_params params;

    if (!gpt_params_parse(argc, argv, params))
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

    // Start LLaVa thread to process incoming requests
    std::thread llava_thread(run_llava_thread, params, model, ctx_params, ctx_clip);

    // Serve forever
    run_web_server("localhost", 8080, [](const llava_request &request) { push_to_work_queue(request); });

    return 0;
}
