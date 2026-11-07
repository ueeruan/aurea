// Host de testes (MSVC): sem `.incbin`, o modelo do clique não entra. RotoClick
// devolve false e o clique segue como um traço pequeno.
namespace aurea::ai::embedded {
const unsigned char* roto_click_encoder_weights() { return nullptr; }
const char* roto_click_encoder_param() { return nullptr; }
const unsigned char* roto_click_decoder_weights() { return nullptr; }
const char* roto_click_decoder_param() { return nullptr; }
const unsigned char* roto_click_prompt() { return nullptr; }
}
