//
//  main.cpp
//  tikbd-capture
//
//  Created by Erik Piehl on 9.7.2022.
//

#include "../include/SDL2/SDL.h"
#include "../include/SDL2/SDL_image.h"
#include <iostream>
#include <map>
#include <unistd.h>
#include <algorithm>  // min, max


SDL_Window* window = nullptr;
int width = 320;
int height = 240;
const char *mytitle = "sprite-rotator";
bool running = true;
SDL_Renderer *renderer = nullptr;
SDL_Texture *texture = nullptr;
SDL_Texture *current = nullptr;
SDL_Texture *assets = nullptr;

int curx = 0;
int cury = 0;
bool draw_toggle = false;
float rot_angle = 0.0f;

const int miniature_scale = 2;
const int miniature_x = 16*8+16;
const int miniature_y = 16;

const int sx=8, sy=8;

static SDL_Color colors[15] = { // r, g, b, a
  0x00, 0x00, 0x00, 0xff,
  0x21, 0xc8, 0x42, 0xff,
  0x5e, 0xdc, 0x78, 0xff,
  0x54, 0x55, 0xed, 0xff,
  0x7d, 0x76, 0xfc, 0xff,
  0xd4, 0x52, 0x4d, 0xff,
  0x42, 0xeb, 0xf5, 0xff,
  0xfc, 0x55, 0x54, 0xff,
  0xff, 0x79, 0x78, 0xff,
  0xd4, 0xc1, 0x54, 0xff,
  0xe6, 0xce, 0x80, 0xff,
  0x21, 0xb0, 0x3b, 0xff,
  0xc9, 0x5b, 0xba, 0xff,
  0xcc, 0xcc, 0xcc, 0xff,
  0xff, 0xff, 0xff, 0xff
};

uint8_t sprite[16][16] = { 0 }; // entries are indeces to colors above

// Supersample, calculate rotation around middle of the sprite.
class super_sampler {
private:
  super_sampler(super_sampler &X) {}  // hide copy constructor
  uint8_t *super_sampled = nullptr;
  int super_ratio=1;
  int size1, size2;
public:
  super_sampler(int ratio) {
    super_ratio = std::min(std::max(ratio,2),16);
    size1 = super_ratio*16;
    size2 = size1/2;
    super_sampled = new uint8_t[size1*size1];
  }
  virtual ~super_sampler() {
    delete[] super_sampled;
  }
  int get_size1() const { return size1; }
  int get_size2() const { return size2; }
  uint8_t *get_buffer() const { return super_sampled; }
  
  void render_at_an_angle(float angle) {
    for(int y=0; y<super_ratio*16; y++) {
      for(int x=0; x<super_ratio*16; x++) {
        float cf = cosf(angle);
        float sf = sinf(angle);
        // x' = xcosθ - ysinθ.
        // y' = xsinθ + ycosθ.
        float x1 = (x-size2)*cf - (y-size2)*sf;
        float y1 = (x-size2)*sf + (y-size2)*cf;
        x1 += size2;
        y1 += size2;
        // Convert to integers and see where we land.
        int x2 = floorf(x1/super_ratio);
        int y2 = floorf(y1/super_ratio);
        // Now x2 and y2 are in theory our sample points.
        if(y2 >= 0 && x2 >= 0 && y2 < 16 && x2 < 16)
          super_sampled[y*size1+x] = sprite[y2][x2];
        else
          super_sampled[y*size1+x] = 0;
      }
    }
  }
  
  void render_to_RGB24(uint8_t *p, unsigned pitch, uint8_t r, uint8_t g, uint8_t b) {
    if(!p)
      return;
    for(int y=0; y<size1; y++) {
      uint8_t *d = p + pitch*y;
      for(int x=0; x<size1; x++) {
        if(super_sampled[y*size1+x]) {
          *d++ = r;
          *d++ = g;
          *d++ = b;
        } else {
          *d++ = 32;
          *d++ = 32;
          *d++ = 32;
        }
      }
    }
  }
  
  void render_down(const super_sampler& X) {
    // Bring down the super sampling by calculating
    // in the source the number of lit pixels and if
    // more than half are lit, we draw a pixel here.
    
    uint8_t mask5[5][5] = {  // sampling mask for 4x4 super_ratio
      0, 0, 1, 0, 0,
      0, 1, 2, 1, 0,
      1, 2, 3, 2, 1,
      0, 1, 2, 1, 0,
      0, 0, 1, 0, 0
    };
    
    for(int y=0; y<16; y++) {
      for(int x=0; x<16; x++) {
        // Next iterate over the source, referenced via X.
        int sum = 0;
        for(int j=0; j<X.super_ratio; j++) {
          const uint8_t *p = &X.super_sampled[(y*X.super_ratio+j)*16*X.super_ratio + x*X.super_ratio];
          for(int i=0; i<X.super_ratio; i++) {
            sum += p[i]*mask5[j][i];
          }
        }
        
        // Now we have our sum. Threshold it.
        uint8_t val = (sum >= X.super_ratio*X.super_ratio/3) ? 1 : 0;
        if(X.super_ratio == 5)
          val = sum >= 8 ? 1 : 0;
        // std::cout << y << "," << x << " " << (int)val << std::endl;
        
        // And draw our fat pixel.
        for(int j=0; j<super_ratio; j++) {
          uint8_t *p = &super_sampled[(y*super_ratio+j)*16*super_ratio + x*super_ratio];
          for(int i=0; i<super_ratio; i++) {
            p[i] = val;
          }
        }
      }
    }
    
  }
};

super_sampler sampler(5);
super_sampler lores(8);

void render_begin() {
  
  SDL_SetRenderTarget(renderer, current);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0xFF);
  SDL_RenderClear(renderer);
  
  // Draw our sprite so far.
  SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
  SDL_Rect r = { miniature_x-1, miniature_y-1, 16*miniature_scale+2, 16*miniature_scale+2 };
  SDL_RenderDrawRect(renderer, &r);
  int ymax  = 0;
  for(int y=0; y<16; y++) {
    for(int x=0; x<16; x++) {
      r.x = x*sx;
      r.y = y*sy;
      r.w = sx;
      r.h = sy;
      SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
      if(sprite[y][x]) {
        SDL_RenderFillRect(renderer, &r);
      } else {
        SDL_RenderDrawRect(renderer, &r);
      }
      
      // Also draw miniature version.
      if(sprite[y][x]) {
        r.x = miniature_x + x*miniature_scale;
        r.y = miniature_y + y*miniature_scale;
        r.w = miniature_scale;
        r.h = miniature_scale;
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderFillRect(renderer, &r);
      }
    }
  }

  // Draw the rotated pictures, supersampled version and fat pixels.
  int height = 240; // sampler.get_size1()+lores.get_size1();
  int width = std::max(sampler.get_size1(), lores.get_size1());
  r = { 200, 0, width, height  };
  SDL_Rect rdest = { 200, 0, width, height };
  SDL_RenderCopy(renderer, texture, &r, &rdest);

  ymax = height;
  


  // Draw the TMS9918 color palette.
  for(int i=0; i<15; i++) {
    r = { i*16, ymax, 16, 16 };
    SDL_SetRenderDrawColor(renderer,
                           colors[i].r, colors[i].g, colors[i].b, colors[i].a);
    SDL_RenderFillRect(renderer, &r);
  }

  // Draw our cursor, flash it 5 times per second.
  uint32_t t = SDL_GetTicks();
  if(t % 200 < 100)
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 0);
  else
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 0);
  r.x = curx*sx;
  r.y = cury*sy;
  r.w = sx;
  r.h = sy;
  SDL_RenderDrawRect(renderer, &r);

}

void render_end() {
  // Show toggle indicator if needed
  if(draw_toggle) {
    SDL_Rect      r         = { 0,     0, 131, 20 };
    SDL_Rect      dest_rect = { 0, 17*sy, r.w, r.h };
    SDL_RenderCopy(renderer, assets, &r, &dest_rect);
  }
  
  // SDL_RenderDrawLine(renderer, 0, y, width-1, y);
  SDL_RenderPresent(renderer);
}

void handle_event(SDL_Event &event) {
  switch (event.type) {
    case SDL_QUIT:
      running = false;
      break;
    case SDL_WINDOWEVENT:
      // if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
      //        blit_renderer->resize(event.window.data1, event.window.data2);
      // }
      break;
    case SDL_MOUSEBUTTONUP:
      break;
    case SDL_MOUSEBUTTONDOWN:
      //blit_input->handle_mouse(event.button.button, event.type == SDL_MOUSEBUTTONDOWN, event.button.x, event.button.y);
      if(event.button.button == SDL_BUTTON_LEFT) {
        // Check if we are choosing color and change color.
      }
      break;
    case SDL_MOUSEMOTION:
      // if (event.motion.state & SDL_BUTTON_LMASK) {
      //        blit_input->handle_mouse(SDL_BUTTON_LEFT, event.motion.state & SDL_MOUSEBUTTONDOWN, event.motion.x, event.motion.y);
      // }
      break;
    case SDL_KEYDOWN: // fall-though
    case SDL_KEYUP:
      {
        // if (!blit_input->handle_keyboard(event.key.keysym.sym, event.type == SDL_KEYDOWN)) {
        // }
        // https://wiki.libsdl.org/SDL_RenderCopy
        
        render_begin();

        int y = event.key.keysym.scancode;
        if(event.type == SDL_KEYDOWN) {
          int oldx = curx, oldy = cury;
          switch(event.key.keysym.sym) {
            case SDLK_DOWN: cury = std::min(cury+1,15); break;
            case SDLK_UP:   cury = std::max(cury-1,0);  break;
            case SDLK_LEFT: curx = std::max(curx-1, 0); break;
            case SDLK_RIGHT:  curx = std::min(curx+1, 15); break;
            case SDLK_t:     draw_toggle = !draw_toggle; break;
            case SDLK_q:     rot_angle += 3.141529f/32; break;
            case SDLK_e:     rot_angle -= 3.141529f/32; break;
            case SDLK_0:     rot_angle = 0; break;
            case SDLK_s:      // Shift down the whole thing
            {
              uint8_t buf[16];
              memcpy(buf, sprite[15], 16); // Backup bottom row
              memmove(sprite[1], sprite[0], 16*15);
              memcpy(sprite[0], buf, 16);
              break;
            }
            case SDLK_w:      // Shift up the whole thing
            {
              uint8_t buf[16];
              memcpy(buf, sprite[0], 16); // Backup top row
              memcpy(sprite[0], sprite[1], 16*15);
              memcpy(sprite[15], buf, 16);
              break;
            }
            case SDLK_a:      // Shift left the whole thing
            {
              for(int i=0; i<16; i++) {
                uint8_t t = sprite[i][0];
                memmove(&sprite[i][0], &sprite[i][1], 15);
                sprite[i][15] = t;
              }
              break;
            }
            case SDLK_d:      // Shift right the whole thing.
            {
              for(int i=0; i<16; i++) {
                uint8_t t = sprite[i][15];
                memmove(&sprite[i][1], &sprite[i][0], 15);
                sprite[i][0] = t;
              }
              break;
            }
            case SDLK_SPACE:
              sprite[cury][curx] ^= 1;
              break;
            case SDLK_ESCAPE:
              break;
          }
          if(cury != oldy || curx != oldx) {
            std::cout << curx << "," << cury << std::endl;
            if(draw_toggle)
              sprite[cury][curx] ^= 1;
          }
          
          if(event.type == SDL_KEYDOWN &&
             (event.key.keysym.sym == SDLK_r || event.key.keysym.sym == SDLK_q || event.key.keysym.sym == SDLK_e)) {
            // SDL_SetRenderTarget(renderer, nullptr);
            // Draw supersampled version...
            uint8_t *p = nullptr;
            int pitch;
            SDL_LockTexture(texture, nullptr, (void **)&p, &pitch);
            if(p) {
              sampler.render_at_an_angle(rot_angle);
              lores.render_down(sampler); // Create a low res version.
              lores.render_to_RGB24(  p + 200*3,                         pitch, 0,   0, 255);
              sampler.render_to_RGB24(p + 200*3+pitch*lores.get_size1(), pitch, 0, 250,    0);
              SDL_UnlockTexture(texture);
            }
          }


        } else {
          // KEYUP
          SDL_SetRenderDrawColor(renderer, 128, 128, 128,0);
        }
        render_end();
        break;
      }
      break;
    case SDL_RENDER_TARGETS_RESET:
      std::cout << "Targets reset" << std::endl;
      break;
    case SDL_RENDER_DEVICE_RESET:
      std::cout << "Device reset" << std::endl;
      break;
    default:
//      if(event.type == System::loop_event) {
//        blit_renderer->update(blit_system);
//        blit_system->notify_redraw();
//        blit_renderer->present();
//      } else
//      if (event.type == System::timer_event) {
        switch(event.user.code) {
          case 0:
            SDL_SetWindowTitle(window, mytitle);
            break;
          case 1:
            SDL_SetWindowTitle(window, (std::string(mytitle) + " [SLOW]").c_str());
            break;
          case 2:
            SDL_SetWindowTitle(window, (std::string(mytitle) + " [FROZEN]").c_str());
            break;
        }
//      }
      break;
    }
}

int main(int argc, const char * argv[]) {
  
  
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
          std::cerr << "could not initialize SDL2: " << SDL_GetError() << std::endl;
          return 1;
  }

  window = SDL_CreateWindow(
          mytitle,
          SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
          width*2, height*2,
          SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
  );

  if (window == nullptr) {
          std::cerr << "could not create window: " << SDL_GetError() << std::endl;
          return 1;
  }
  
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == nullptr) {
    std::cerr << "could not create renderer: " << SDL_GetError() << std::endl;
    return 1;
  }
  
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, /* SDL_TEXTUREACCESS_TARGET | */ SDL_TEXTUREACCESS_STREAMING, width, height);
  if(texture == nullptr) {
    std::cerr << "could not create texture: " << SDL_GetError() << std::endl;
    return 1;
  }
  
  current = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, /* SDL_TEXTUREACCESS_TARGET | */ SDL_TEXTUREACCESS_STREAMING, width, height);
  if(current == nullptr) {
    std::cerr << "could not create texture: " << SDL_GetError() << std::endl;
    return 1;
  }
  
  IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
  assets = IMG_LoadTexture(renderer, "../../../sprite-rotator-assets.png");
  if(assets == nullptr) {
    char s[200];
    s[0] = 0;
    if(getcwd(s, sizeof(s))) {
      std::cerr << "current directory: " << s << std::endl;
    }

    std::cerr << "unable to open assets: " << SDL_GetError()  << std::endl;
    return 1;
  }

  // Clear the window.
  SDL_SetRenderTarget(renderer, texture);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  SDL_RenderPresent(renderer);

  // Try to load our sprite.
  FILE *f = fopen("sprite.bin", "r");
  if(f) {
    fread(sprite, sizeof(uint8_t), 16*16, f);
    fclose(f);
  }

  
  SDL_SetWindowMinimumSize(window, width, height);
  
  // Render the stuff.
  render_begin();
  render_end();
  
  int count=0;
  SDL_Event event;
  while (running) {
    while(running && SDL_PollEvent(&event) != 0)
      handle_event(event);
    count++;
    render_begin();
    render_end();
  }
  
  // Try to save our sprite.
  f = fopen("sprite.bin", "w");
  if(f) {
    fwrite(sprite, sizeof(uint8_t), 16*16, f);
    fclose(f);
  }
  
  SDL_DestroyTexture(assets);
  SDL_DestroyTexture(texture);
  SDL_DestroyTexture(current);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  
  return 0;
}
