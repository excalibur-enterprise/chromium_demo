#include "base/at_exit.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/i18n/icu_util.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_pump_type.h"
#include "base/path_service.h"
#include "base/power_monitor/power_monitor.h"
#include "base/power_monitor/power_monitor_device_source.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/task_environment.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "components/viz/common/quads/solid_color_draw_quad.h"
#include "components/viz/demo/host/demo_host.h"
#include "components/viz/demo/service/demo_service.h"
#include "components/viz/host/host_frame_sink_manager.h"
#include "components/viz/host/renderer_settings_creation.h"
#include "components/viz/service/display_embedder/server_shared_bitmap_manager.h"
#include "components/viz/service/frame_sinks/frame_sink_manager_impl.h"
#include "components/viz/service/main/viz_compositor_thread_runner_impl.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/viz/privileged/mojom/viz_main.mojom.h"
#include "ui/base/hit_test.h"
#include "ui/base/ime/init/input_method_initializer.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/base/x/x11_util.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/skia_util.h"
#include "ui/gl/gl_switches.h"
#include "ui/gl/init/gl_factory.h"
#include "ui/platform_window/platform_window.h"
#include "ui/platform_window/platform_window_delegate.h"
#include "ui/platform_window/platform_window_init_properties.h"
#include "ui/platform_window/x11/x11_window.h"

#include "GL/gl.h"
#include "gpu/command_buffer/service/feature_info.h"
#include "gpu/command_buffer/service/gr_shader_cache.h"
#include "gpu/command_buffer/service/shared_context_state.h"
#include "gpu/config/gpu_driver_bug_workarounds.h"
#include "gpu/config/gpu_info_collector.h"
#include "gpu/config/gpu_preferences.h"
#include "gpu/config/gpu_util.h"
#include "gpu/ipc/common/gpu_client_ids.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/gpu/GrBackendSurface.h"
#include "third_party/skia/include/gpu/gl/GrGLTypes.h"
#include "third_party/skia/src/gpu/gl/GrGLDefines.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/init/gl_factory.h"

#include "ui/gfx/geometry/size.h"
#include "ui/gl/gl_share_group.h"

#include "demo/common/utils.h"

#if defined(USE_AURA)
#include "ui/aura/env.h"
#include "ui/wm/core/wm_state.h"
#endif

#if defined(USE_X11)
#include "ui/base/x/x11_util_internal.h"
#include "ui/gfx/x/x11_atom_cache.h"
#include "ui/gfx/x/x11_connection.h"  // nogncheck
#include "ui/gl/gl_surface_glx.h"
#include "ui/gl/gl_visual_picker_glx.h"
#include "ui/platform_window/x11/x11_window.h"  // nogncheck
#endif

#if defined(USE_OZONE)
#include "ui/ozone/public/ozone_platform.h"
#endif

#if defined(OS_WIN)
#include "ui/base/cursor/cursor_loader_win.h"
#include "ui/platform_window/win/win_window.h"
#endif

namespace demo {
// DemoWindow creates the native window for the demo app. The native window
// provides a gfx::AcceleratedWidget, which is needed for the display
// compositor.
class DemoWindowHost : public ui::PlatformWindowDelegate {
 public:
  DemoWindowHost(base::OnceClosure close_closure)
      : close_closure_(std::move(close_closure)) {}
  ~DemoWindowHost() override = default;

  void Create(const gfx::Rect& bounds) {
    platform_window_ = CreatePlatformWindow(bounds);
    platform_window_->Show();

    if (widget_ != gfx::kNullAcceleratedWidget)
      InitializeDemo();
  }

 private:
  std::unique_ptr<ui::PlatformWindow> CreatePlatformWindow(
      const gfx::Rect& bounds) {
    ui::PlatformWindowInitProperties props(bounds);
    props.type = ui::PlatformWindowType::kWindow;
    // props.opacity = ui::PlatformWindowOpacity::kTranslucentWindow;
    // props.background_color = 0;
#if defined(USE_OZONE)
    return ui::OzonePlatform::GetInstance()->CreatePlatformWindow(
        this, std::move(props));
#elif defined(OS_WIN)
    return std::make_unique<ui::WinWindow>(this, props.bounds);
#elif defined(USE_X11)
    auto x11_window = std::make_unique<ui::X11Window>(this);
    x11_window->Initialize(std::move(props));
    return x11_window;
#else
    NOTIMPLEMENTED();
    return nullptr;
#endif
  }

  void InitializeDemo() {
    DCHECK_NE(widget_, gfx::kNullAcceleratedWidget);
    TRACE_EVENT0("shell", "InitializeDemo");
    if (!g_gl_surface) {
      gl::init::InitializeGLOneOff();

      g_gl_surface = gl::init::CreateViewGLSurface(widget_);

      scoped_refptr<gl::GLShareGroup> share_group =
          base::MakeRefCounted<gl::GLShareGroup>();
      g_gl_context = gl::init::CreateGLContext(
          share_group.get(), g_gl_surface.get(), gl::GLContextAttribs());
      DCHECK(g_gl_context->MakeCurrent(g_gl_surface.get()));
      g_context_state = base::MakeRefCounted<gpu::SharedContextState>(
          std::move(share_group), g_gl_surface, g_gl_context, false,
          base::DoNothing(), gpu::GrContextType::kGL);
      
      gpu::GpuPreferences gpu_preferences;
      gpu::GpuFeatureInfo gpu_feature_info;
      gpu::GpuDriverBugWorkarounds workarounds;
      scoped_refptr<gpu::gles2::FeatureInfo> feature_info =
          new gpu::gles2::FeatureInfo(workarounds, gpu_feature_info);
      g_context_state->InitializeGL(gpu_preferences, feature_info);
      g_context_state->InitializeGrContext(feature_info->workarounds(),
                                           nullptr);
    }
    DCHECK(g_context_state->MakeCurrent(g_gl_surface.get(), true));
    DCHECK(g_context_state->gr_context());
    static unsigned int i = 0;
    glClearColor(1.f, (i++) % 10 / 10.f + 0.1f, 0, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    g_gl_surface->SwapBuffers(base::DoNothing());
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&DemoWindowHost::InitializeDemo, base::Unretained(this)),
        base::TimeDelta::FromSeconds(1));
  }

  // ui::PlatformWindowDelegate:
  void OnBoundsChanged(const gfx::Rect& new_bounds) override {}

  void OnAcceleratedWidgetAvailable(gfx::AcceleratedWidget widget) override {
    widget_ = widget;
    // 如果需要去除窗口边框，将true改为false
    ui::SetUseOSWindowFrame(widget_, true);
    if (platform_window_)
      InitializeDemo();
  }

  void OnDamageRect(const gfx::Rect& damaged_region) override {}
  void DispatchEvent(ui::Event* event) override {
    if ((event->IsMouseEvent() && event->AsMouseEvent()->IsLeftMouseButton()) ||
        event->IsTouchEvent()) {
      int action = -1;
      if (event->type() == ui::ET_MOUSE_PRESSED ||
          event->type() == ui::ET_TOUCH_PRESSED)
        action = 0;
      else if (event->type() == ui::ET_MOUSE_RELEASED ||
               event->type() == ui::ET_TOUCH_RELEASED)
        action = 1;
      else if (event->type() == ui::ET_MOUSE_DRAGGED ||
               event->type() == ui::ET_TOUCH_MOVED)
        action = 2;
      else
        return;
      auto* located_event = event->AsLocatedEvent();
      auto location = located_event->location();
      if (action != 2)
        DLOG(INFO) << "action,x,y= " << action << "," << location.x() << ","
                   << location.y();
    }
  }
  void OnCloseRequest() override {
    // TODO: Use a more robust exit method

    platform_window_->Close();
  }
  void OnClosed() override {
    if (close_closure_)
      std::move(close_closure_).Run();
  }
  void OnWindowStateChanged(ui::PlatformWindowState new_state) override {}
  void OnLostCapture() override {}
  void OnAcceleratedWidgetDestroyed() override {}
  void OnActivationChanged(bool active) override {}
  void OnMouseEnter() override {}

  std::unique_ptr<ui::PlatformWindow> platform_window_;
  gfx::AcceleratedWidget widget_;
  base::OnceClosure close_closure_;
  //bool is_software_ = true;
  scoped_refptr<gl::GLSurface> g_gl_surface;
  scoped_refptr<gl::GLContext> g_gl_context;
  scoped_refptr<gpu::SharedContextState> g_context_state;

  DISALLOW_COPY_AND_ASSIGN(DemoWindowHost);
};

}  // namespace demo

int main(int argc, char** argv) {
  // 类似C++的 atexit() 方法，用于管理程序的销毁逻辑，base::Singleton类依赖它
  base::AtExitManager at_exit;
  // 初始化CommandLine
  base::CommandLine::Init(argc, argv);
  // 设置日志格式
  logging::SetLogItems(true, true, true, false);
  // 启动 Trace
  demo::InitTrace("./trace_demo_gl.json");
  demo::StartTrace("gpu,shell,disabled-by-default-toplevel.flow");
  // 创建主消息循环，等价于 MessagLoop
  base::SingleThreadTaskExecutor main_task_executor(base::MessagePumpType::UI);
  // 初始化线程池，会创建新的线程，在新的线程中会创建新消息循环MessageLoop
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("DemoGL");

  // 在Linux上，x11和aura都是默认开启的
#if defined(USE_X11)
  // This demo uses InProcessContextFactory which uses X on a separate Gpu
  // thread.
  gfx::InitializeThreadedX11();

  // 设置X11的异常处理回调，如果不设置在很多设备上会频繁出现崩溃。
  // 比如 ui::XWindow::Close() 和~SGIVideoSyncProviderThreadShim 的析构中
  // 都调用了 XDestroyWindow() ，并且是在不同的线程中调用的，当这两个窗口有
  // 父子关系的时候，如果先调用了父窗口的销毁再调用子窗口的销毁则会导致BadWindow
  // 错误，默认的Xlib异常处理会打印错误日志然后强制结束程序。
  // 这些错误大多是并发导致的代码执行顺序问题，所以修改起来没有那么容易。
  ui::SetDefaultX11ErrorHandlers();
#endif

  auto event_source_ = ui::PlatformEventSource::CreateDefault();

  // 初始化ICU(i18n),也就是icudtl.dat，views依赖ICU
  base::i18n::InitializeICU();

  ui::RegisterPathProvider();

  // This app isn't a test and shouldn't timeout.
  base::RunLoop::ScopedDisableRunTimeoutForTest disable_timeout;

  base::RunLoop run_loop;

  demo::DemoWindowHost window(run_loop.QuitClosure());
  window.Create(gfx::Rect(800, 600));

  LOG(INFO) << "running...";
  run_loop.Run();

  return 0;
}
