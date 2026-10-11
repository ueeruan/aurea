#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <cstring>
#include <vector>
#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"

namespace aurea::android {
// Keyflow's encoder contract: a recordable EGL surface, exact presentation
// time, one draw/swap followed by output drain. The Aurea scene stays in its
// renderer; this bridge uploads its finished NV12 frame without relying on
// a vendor codec's CPU input-buffer strides.
class KeyflowEncoderSurface {
public:
    ~KeyflowEncoderSurface() { close(); }
    Status open(ANativeWindow* window, u32 width, u32 height) noexcept {
        width_ = width; height_ = height;
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) return error();
        const EGLint attributes[] = {EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,
            EGL_ALPHA_SIZE,8,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
            0x3142,1,EGL_NONE}; // EGL_RECORDABLE_ANDROID
        EGLConfig config{}; EGLint count = 0;
        if (!eglChooseConfig(display_, attributes, &config, 1, &count) || count != 1) return error();
        const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttributes);
        surface_ = eglCreateWindowSurface(display_, config, window, nullptr);
        presentation_ = reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(eglGetProcAddress("eglPresentationTimeANDROID"));
        if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE || !presentation_) return error();
        return OkStatus;
    }
    Status submit(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 ptsUs) noexcept {
        if (!y || !uv || ys < width_ || uvs < width_ || ptsUs < 0) return Errc::InvalidArgument;
        if (!eglMakeCurrent(display_, surface_, surface_, context_)) return error();
        struct Unbind { EGLDisplay d; ~Unbind() { eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT); } } unbind{display_};
        if (!program_ && !initialize_gl()) return error();
        upload(y, ys, width_, height_, GL_LUMINANCE, 0);
        upload(uv, uvs, width_, height_/2, GL_LUMINANCE_ALPHA, 1);
        glViewport(0,0,width_,height_); glUseProgram(program_);
        glUniform1i(glGetUniformLocation(program_,"planeY"),0);
        glUniform1i(glGetUniformLocation(program_,"planeUV"),1);
        const GLfloat vertices[] = {-1,-1,1,-1,-1,1,1,1};
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices); glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        if (glGetError() != GL_NO_ERROR || !presentation_(display_,surface_,ptsUs*1000)
            || !eglSwapBuffers(display_,surface_)) return error();
        return OkStatus;
    }
    void close() noexcept {
        if (display_ != EGL_NO_DISPLAY) {
            if (context_ != EGL_NO_CONTEXT && surface_ != EGL_NO_SURFACE
                && eglMakeCurrent(display_,surface_,surface_,context_)) {
                if (program_) glDeleteProgram(program_);
                glDeleteTextures(2,textures_);
                eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
            }
            if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_,surface_);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_,context_);
            // EGL_DEFAULT_DISPLAY may also belong to Aurea's preview backend.
            // Do not terminate a display shared with another renderer.
        }
        display_=EGL_NO_DISPLAY; context_=EGL_NO_CONTEXT; surface_=EGL_NO_SURFACE;
        program_=0; textures_[0]=textures_[1]=0; uploaded_[0]=uploaded_[1]=false; packed_.clear();
    }
private:
    Status error() const noexcept { return Status{Errc::EncodeFailed,"Keyflow: falha na superficie EGL do encoder"}; }
    GLuint shader(GLenum type, const char* source) noexcept {
        GLuint s=glCreateShader(type); glShaderSource(s,1,&source,nullptr); glCompileShader(s);
        GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
        if (!ok) { glDeleteShader(s); return 0; } return s;
    }
    bool initialize_gl() noexcept {
        const char* vs="attribute vec2 position; varying vec2 uv; void main(){gl_Position=vec4(position,0.,1.);uv=vec2((position.x+1.)*.5,(1.-position.y)*.5);}";
        const char* fs="precision highp float; varying vec2 uv; uniform sampler2D planeY; uniform sampler2D planeUV; void main(){float y=(texture2D(planeY,uv).r-16./255.)*255./219.; vec2 c=(texture2D(planeUV,uv).ra-vec2(128./255.))*255./224.; gl_FragColor=vec4(y+1.5748*c.y,y-.187324*c.x-.468124*c.y,y+1.8556*c.x,1.);}";
        GLuint v=shader(GL_VERTEX_SHADER,vs), f=shader(GL_FRAGMENT_SHADER,fs);
        if (!v || !f) { if(v)glDeleteShader(v); if(f)glDeleteShader(f); return false; }
        program_=glCreateProgram(); glAttachShader(program_,v); glAttachShader(program_,f);
        glBindAttribLocation(program_,0,"position"); glLinkProgram(program_);
        glDeleteShader(v); glDeleteShader(f); GLint ok=0; glGetProgramiv(program_,GL_LINK_STATUS,&ok);
        if (!ok) { glDeleteProgram(program_); program_=0; return false; }
        glGenTextures(2,textures_); glPixelStorei(GL_UNPACK_ALIGNMENT,1); return true;
    }
    void upload(const u8* source,u32 stride,u32 bytes,u32 rows,GLenum format,u32 plane) {
        if (stride != bytes) {
            packed_.resize(static_cast<size_t>(bytes)*rows);
            for(u32 r=0;r<rows;++r) std::memcpy(packed_.data()+static_cast<size_t>(r)*bytes,source+static_cast<size_t>(r)*stride,bytes);
            source=packed_.data();
        }
        glActiveTexture(GL_TEXTURE0+plane); glBindTexture(GL_TEXTURE_2D,textures_[plane]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        const u32 texWidth=plane ? bytes/2 : bytes;
        if (!uploaded_[plane]) {
            glTexImage2D(GL_TEXTURE_2D,0,format,texWidth,rows,0,format,GL_UNSIGNED_BYTE,source);
            uploaded_[plane]=true;
        } else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,texWidth,rows,format,GL_UNSIGNED_BYTE,source);
    }
    EGLDisplay display_=EGL_NO_DISPLAY; EGLContext context_=EGL_NO_CONTEXT;
    EGLSurface surface_=EGL_NO_SURFACE; PFNEGLPRESENTATIONTIMEANDROIDPROC presentation_=nullptr;
    GLuint program_=0, textures_[2]{}; bool uploaded_[2]{}; u32 width_=0,height_=0;
    std::vector<u8> packed_;
};
}
