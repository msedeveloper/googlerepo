/*
 * Copyright 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * This test measures rendering performance degradation through indirect texture reading (AKA,
 * dependent texture reading). The test starts with a simple image rendering without indirect
 * texture reading. That is to say, in the first round, the fragment shader colors each texel as it
 * originally comes from the texture.
 * However on each subsequent rendering round, the fragment shader will apply a displacement to the
 * received texture coordinates. The displacement is the result of sampling the original texture
 * coordinates and use the red component as a normalized delta x, the green component as a
 * normalized delta y. Thus, to the received texture coordinates (x,y), a (x + dx, y + dy) is
 * computed and re-sampled). That would make one (1) indirection.
 * At regular intervals, the number of indirections is increased. So the original (x, y) texture
 * coordinates are sampled in order to be displaced (dx, dy), and the resulting texture coordinate
 * is, again, sampled in order to be displaced.
 * To recap so far, in the first interval there are no displacements, in the second one there's one
 * displacement, in the third two displacements and so on.
 *
 * Input configuration:
 * - indirection_interval (optional, default 5s): duration of a given indirection round. At the end
 *                                                of each interval, the number of these indirections
 *                                                is increased.
 * - report_interval (optional, default 100ms): duration between result reports.
 *
 * Output report:
 * - indirections: number of times that texture coordinates are used to compute a displacement.
 * - fps: frames per second.
 */

#include <chrono>
#include <memory>
#include <random>

#include <ancer/BaseGLES3Operation.hpp>
#include <ancer/DatumReporting.hpp>
#include <ancer/util/Error.hpp>
#include <ancer/util/GLTexImage2DRenderer.hpp>
#include <ancer/util/Json.hpp>

using namespace ancer;

//==================================================================================================

namespace {
constexpr Log::Tag TAG{"DependentTextureRead"};
}

//==================================================================================================

namespace {
struct configuration {
  Seconds indirection_interval = 5s;
  Milliseconds report_interval = 100ms;
};

JSON_CONVERTER(configuration) {
  JSON_OPTVAR(indirection_interval);
  JSON_OPTVAR(report_interval);
}

//--------------------------------------------------------------------------------------------------

struct throughput {
  uint indirections;
  float fps;
  Nanoseconds maximum_frame_time;
};

void WriteDatum(report_writers::Struct writer, const throughput &throughput) {
  ADD_DATUM_MEMBER(writer, throughput, indirections);
  ADD_DATUM_MEMBER(writer, throughput, fps);
  ADD_DATUM_MEMBER(writer, throughput, maximum_frame_time);
}
}

//==================================================================================================

class DependentTextureReadGLES3Operation : public BaseGLES3Operation {
 public:
  DependentTextureReadGLES3Operation() = default;

  ~DependentTextureReadGLES3Operation() override {
    glDeleteProgram(_program);
    glDeleteTextures(1, &_tex_id);
  }

  void OnGlContextReady(const GLContextConfig &ctx_config) override {
    SetHeartbeatPeriod(250ms);

    _configuration = GetConfiguration<configuration>();

    _egl_context = eglGetCurrentContext();
    if (_egl_context==EGL_NO_CONTEXT) {
      FatalError(TAG, "No EGL context available");
    }

    _tex_id = SetupTexture();

    glDisable(GL_BLEND);

    _program = SetupProgram();

    _generator = SetupRandomMultiplierGenerator();

    _random_multiplier_uniform_loc = glGetUniformLocation(_program, "uMultiplier");
    _indirections_uniform_loc = glGetUniformLocation(_program, "uIndirections");

    _fps_calculator = &(GetFpsCalculator());

    _context_ready = true;
  }

  // Depending on screen sizes, tries to fit the projection as a 1:1 square right in the middle.
  void OnGlContextResized(int width, int height) override {
    BaseGLES3Operation::OnGlContextResized(width, height);
    int left{0}, top{0}, right{0}, bottom{0};

    if (width > height) {
      left = (height - width)/2;
      right = width - left;
      bottom = height;
    } else {
      top = (width - height)/2;
      right = width;
      bottom = height - top;
    }

    _projection = glh::Ortho2d(left, top, right, bottom);

    if (_context_ready) {
      SetupRenderer();
    }
  }

  void OnHeartbeat(Duration elapsed) override {
    if (GetMode()==Mode::DataGatherer) {
      _report_timer += elapsed;
      if (_configuration.report_interval > Duration::zero() &&
          _report_timer >= _configuration.report_interval) {
        _report_timer -= _configuration.report_interval;
        Report(throughput{static_cast<uint >(_indirections),
                          _fps_calculator->GetAverageFps(),
                          _fps_calculator->GetMaxFrameTime()
        });
      }
    }

    _indirection_timer += elapsed;
    if (_configuration.indirection_interval > Duration::zero() &&
        _indirection_timer >= _configuration.indirection_interval) {
      _indirection_timer -= _configuration.indirection_interval;
      ++_indirections;
    }
  }

  void Draw(double delta_seconds) override {
    BaseGLES3Operation::Draw(delta_seconds);
    glh::CheckGlError("DependentTextureReadGLES3Operation::Draw(delta_seconds) - superclass");

    glUseProgram(_program);
    glh::CheckGlError("DependentTextureReadGLES3Operation::Draw(delta_seconds) - glUseProgram");

    glUniformMatrix4fv(_projection_uniform_loc, 1, GL_FALSE, glm::value_ptr(_projection));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _tex_id);
    glUniform1i(_tex_id_uniform_loc, 0);

    glUniform1f(_random_multiplier_uniform_loc, static_cast<GLfloat>(_distribution(_generator)));
    glUniform1i(_indirections_uniform_loc, static_cast<GLint>(_indirections));

    if (_renderer) {
      _renderer->Draw();
    }
  }

//--------------------------------------------------------------------------------------------------

 private:
  /**
   * Creates a shader program based on app assets.
   * @return the program id.
   */
  GLuint SetupProgram() {
    auto vertex_file = "Shaders/DependentTextureReadGLES3Operation/pass_through.vsh";
    auto fragment_file = "Shaders/DependentTextureReadGLES3Operation/indirect_texture_read.fsh";

    GLuint program_id = CreateProgram(vertex_file, fragment_file);

    if (!program_id) {
      FatalError(TAG, "Unable to create shader program");
    }

    _tex_id_uniform_loc = glGetUniformLocation(program_id, "uTex");
    glh::CheckGlError("looking up uTex");

    _projection_uniform_loc = glGetUniformLocation(program_id, "uProjection");
    glh::CheckGlError("looking up uProjection");

    return program_id;
  }

  /**
   * Loads a texture, aborting the execution in case of failure.
   * @return the texture ID.
   */
  static GLuint SetupTexture() {
    int tex_width = 0;
    int tex_height = 0;

    GLuint tex_id = LoadTexture("Textures/sphinx.png", &tex_width, &tex_height, nullptr);
    if (tex_width==0 || tex_height==0) {
      FatalError(TAG, "Unable to load texture");
    }

    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    return tex_id;
  }

  /**
   * Creates a true random multiplier generator based on the clock.
   * @return the random multiplier generator.
   */
  static inline std::mt19937 SetupRandomMultiplierGenerator() {
    return std::mt19937(static_cast<unsigned long>(SteadyClock::now().time_since_epoch().count()));
  }

  /**
   * Depending on the total of renderers to build, it will set an NxN grid where N is the minimum
   * layout dimension that can hold all renderers.
   */
  void SetupRenderer() {
    auto size = GetGlContextSize();
    _renderer = std::make_unique<GLTexImage2DRenderer>(0, 0, size.x, size.y);
  }

//--------------------------------------------------------------------------------------------------

 private:
  EGLContext _egl_context = nullptr;
  GLuint _program = 0;

  GLuint _tex_id = 0;
  GLint _tex_id_uniform_loc = -1;

  mat4 _projection;
  GLint _projection_uniform_loc = -1;

  GLint _indirections_uniform_loc = -1;

  GLint _random_multiplier_uniform_loc = -1;

//--------------------------------------------------------------------------------------------------

 private:
  configuration _configuration{};
  bool _context_ready = false;

  std::unique_ptr<GLTexImage2DRenderer> _renderer;
  FpsCalculator *_fps_calculator;

  Duration _report_timer;

  std::mt19937 _generator;
  std::uniform_real_distribution<float> _distribution{0.0, 1.0};
  int _indirections;
  Duration _indirection_timer;

};

EXPORT_ANCER_OPERATION(DependentTextureReadGLES3Operation)
