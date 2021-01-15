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
#include "base/rand_util.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/task_environment.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread.h"
#include "base/trace_event/trace_buffer.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "cc/base/switches.h"
#include "components/discardable_memory/service/discardable_shared_memory_manager.h"
#include "components/viz/client/client_resource_provider.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/viz/common/frame_sinks/copy_output_util.h"
#include "components/viz/common/quads/debug_border_draw_quad.h"
#include "components/viz/common/quads/picture_draw_quad.h"
#include "components/viz/common/quads/solid_color_draw_quad.h"
#include "components/viz/common/quads/surface_draw_quad.h"
#include "components/viz/common/quads/texture_draw_quad.h"
#include "components/viz/common/quads/tile_draw_quad.h"
#include "components/viz/common/quads/video_hole_draw_quad.h"
#include "components/viz/common/resources/bitmap_allocation.h"
#include "components/viz/common/resources/resource_format.h"
#include "components/viz/common/switches.h"
#include "components/viz/demo/host/demo_host.h"
#include "components/viz/demo/service/demo_service.h"
#include "components/viz/host/gpu_host_impl.h"
#include "components/viz/host/host_frame_sink_manager.h"
#include "components/viz/host/host_gpu_memory_buffer_manager.h"
#include "components/viz/host/renderer_settings_creation.h"
#include "components/viz/service/display_embedder/server_shared_bitmap_manager.h"
#include "components/viz/service/frame_sinks/frame_sink_manager_impl.h"
#include "components/viz/service/gl/gpu_service_impl.h"
#include "components/viz/service/main/viz_compositor_thread_runner_impl.h"
#include "components/viz/service/main/viz_main_impl.h"
#include "content/public/common/content_switches.h"
#include "demo/common/utils.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/command_buffer/client/gpu_memory_buffer_manager.h"
#include "gpu/command_buffer/client/shared_image_interface.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "gpu/command_buffer/service/gpu_switches.h"
#include "gpu/command_buffer/service/service_utils.h"
#include "gpu/config/gpu_finch_features.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "gpu/ipc/common/gpu_memory_buffer_support.h"
#include "gpu/ipc/host/shader_disk_cache.h"
#include "gpu/ipc/service/gpu_init.h"
#include "ipc/ipc_channel.h"
#include "ipc/ipc_channel_mojo.h"
#include "ipc/ipc_logging.h"
#include "ipc/ipc_sync_channel.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/resource_coordinator/public/mojom/memory_instrumentation/constants.mojom-forward.h"
#include "services/viz/privileged/mojom/viz_main.mojom.h"
#include "services/viz/public/cpp/gpu/context_provider_command_buffer.h"
#include "third_party/skia/include/core/SkImageEncoder.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/gpu/GrTypes.h"
#include "third_party/skia/src/gpu/gl/GrGLDefines.h"
#include "ui/base/hit_test.h"
#include "ui/base/ime/init/input_method_initializer.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/base/x/x11_util.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/compositor/test/in_process_context_factory.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/platform/platform_event_observer.h"
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

#if defined(USE_AURA)
#include "ui/aura/env.h"
#include "ui/wm/core/wm_state.h"
#endif

#if defined(USE_X11)
#include "ui/gfx/x/x11_connection.h"            // nogncheck
#include "ui/platform_window/x11/x11_window.h"  // nogncheck
#endif

#if defined(USE_OZONE)
#include "ui/ozone/public/ozone_platform.h"
#endif

#if defined(OS_WIN)
#include "ui/base/cursor/cursor_loader_win.h"
#include "ui/platform_window/win/win_window.h"
#endif

bool g_use_gpu = true;

namespace demo {
namespace {
constexpr SkColor colors[] = {SK_ColorRED, SK_ColorGREEN, SK_ColorYELLOW};
// Global atomic to generate child process unique IDs.
base::AtomicSequenceNumber g_unique_id;
}  // namespace

class InkClient : public viz::mojom::CompositorFrameSinkClient,
                  public ui::PlatformEventObserver {
 public:
  InkClient(const viz::FrameSinkId& frame_sink_id,
            const viz::LocalSurfaceIdAllocation& local_surface_id,
            const gfx::Rect& bounds)
      : frame_sink_id_(frame_sink_id),
        local_surface_id_(local_surface_id),
        bounds_(bounds),
        thread_("Demo_" + frame_sink_id.ToString()) {
    CHECK(thread_.Start());
  }
  ~InkClient() override {}
  void Bind(
      mojo::PendingReceiver<viz::mojom::CompositorFrameSinkClient> receiver,
      mojo::PendingRemote<viz::mojom::CompositorFrameSink> remote) {
    if (thread_.task_runner()->BelongsToCurrentThread()) {
      receiver_.Bind(std::move(receiver));
      frame_sink_remote_.Bind(std::move(remote));

      // 告诉 CompositorFrameSink 可以开始请求 CompositorFrame 了
      frame_sink_remote_->SetNeedsBeginFrame(true);
      client_resource_provider_ =
          std::make_unique<viz::ClientResourceProvider>(false);

      // 创建要渲染的内容到 SkBitmap 中
      bitmap_ = std::make_unique<SkBitmap>();
      bitmap_->allocPixels(SkImageInfo::Make(bounds_.width(), bounds_.height(), kRGBA_8888_SkColorType,
                                     kOpaque_SkAlphaType));
      canvas_ = std::make_unique<SkCanvas>(*bitmap_);
      canvas_->clear(SK_ColorWHITE);
      path_.moveTo(0, 0);
      paint_.setColor(SK_ColorRED);
      paint_.setStyle(SkPaint::kStroke_Style);
      paint_.setStrokeWidth(5);
      need_redraw_ = true;
    } else {
      ui::PlatformEventSource::GetInstance()->AddPlatformEventObserver(this);
      thread_.task_runner()->PostTask(
          FROM_HERE, base::BindOnce(&InkClient::Bind, base::Unretained(this),
                                    std::move(receiver), std::move(remote)));
    }
  }

  void SetContextProvider(
      scoped_refptr<viz::ContextProvider> context_provider) {
    if (thread_.task_runner()->BelongsToCurrentThread()) {
      LOG(INFO) << "SetContextProvider";
      context_provider_ = context_provider;
      DCHECK(context_provider->BindToCurrentThread() ==
             gpu::ContextResult::kSuccess);
    } else {
      thread_.task_runner()->PostTask(
          FROM_HERE, base::BindOnce(&InkClient::SetContextProvider,
                                    base::Unretained(this), context_provider));
    }
  }

 private:
  // This is called before the dispatcher receives the event.
  void WillProcessEvent(const ui::PlatformEvent& event) override {}

  // This is called after the event has been dispatched to the dispatcher(s).
  void DidProcessEvent(const ui::PlatformEvent& event) override {
    ui::EventType type = ui::EventTypeFromNative(event);
    std::unique_ptr<ui::LocatedEvent> located_event;
    if (type == ui::ET_MOUSE_MOVED) {
      located_event = std::make_unique<ui::MouseEvent>(event);
    } else if (type == ui::ET_TOUCH_MOVED) {
      located_event = std::make_unique<ui::TouchEvent>(event);
    }
    if (located_event) {
      thread_.task_runner()->PostTask(
          FROM_HERE, base::BindOnce(&InkClient::Draw, base::Unretained(this),
                                    located_event->location()));
    }
  }

  void Draw(const gfx::Point location) {
    TRACE_EVENT1("viz", "LayerTreeFrameSink::Draw", "points_count",
                 path_.countPoints());
    TRACE_COUNTER1("viz", "points_count", path_.countPoints());
    canvas_->clear(SK_ColorWHITE);
    path_.lineTo(location.x(), location.y());
    canvas_->drawPath(path_, paint_);
    need_redraw_ = true;
    frame_sink_remote_->SetNeedsBeginFrame(true);
  }

  viz::CompositorFrame CreateFrame(const ::viz::BeginFrameArgs& args) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::CreateFrame");
    // LOG(INFO) << "CreateFrame: " << frame_sink_id_ << ":" <<
    // local_surface_id_;

    viz::CompositorFrame frame;
    frame.metadata.begin_frame_ack = viz::BeginFrameAck(args, true);
    frame.metadata.device_scale_factor = 1.f;
    frame.metadata.local_surface_id_allocation_time =
        local_surface_id_.allocation_time();
    frame.metadata.frame_token = ++frame_token_generator_;
    frame.metadata.send_frame_token_to_embedder = true;

    const int kRenderPassId = 1;
    const gfx::Rect& output_rect = bounds_;
    const gfx::Rect& damage_rect = output_rect;
    std::unique_ptr<viz::RenderPass> render_pass = viz::RenderPass::Create();
    render_pass->SetNew(kRenderPassId, output_rect, damage_rect,
                        gfx::Transform());

    AppendTextureDrawQuad(frame, render_pass.get());
    AppendSolidColorDrawQuad(frame, render_pass.get());

    frame.render_pass_list.push_back(std::move(render_pass));

    return frame;
  }

  // 演示 TextureDrawQuad 的使用
  void AppendTextureDrawQuad(viz::CompositorFrame& frame,
                             viz::RenderPass* render_pass) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::AppendTextureDrawQuad");

    gfx::Size tile_size(bounds_.width(), bounds_.height());
    // 将 SkBitmap 中的数据转换为资源
    viz::ResourceId resource = CreateResource(tile_size, *bitmap_);

    gfx::Rect output_rect(bounds_);
    gfx::Transform transform;
    // transform.Translate(350, 50);

    auto* quad_state = render_pass->CreateAndAppendSharedQuadState();
    quad_state->SetAll(
        transform,
        /*quad_layer_rect=*/output_rect,
        /*visible_quad_layer_rect=*/output_rect,
        /*rounded_corner_bounds=*/gfx::RRectF(),
        /*clip_rect=*/output_rect,
        /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
        /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

    auto* texture_quad =
        render_pass->CreateAndAppendDrawQuad<viz::TextureDrawQuad>();
    float vertex_opacity[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // 将 resource 添加到 tile_quad 中
    texture_quad->SetNew(quad_state, output_rect, output_rect, false, resource,
                         true, gfx::PointF(0.f, 0.f), gfx::PointF(1.f, 1.f),
                         SK_ColorGRAY, vertex_opacity, false, false, false,
                         gfx::ProtectedVideoType::kClear);

    // 将 resource 对用的资源添加到 frame.resource_list
    // 中，在最简单的情况下可以直接使用 frame.resource_list.push_back(...)
    // 来添加
    client_resource_provider_->PrepareSendToParent(
        {resource}, &frame.resource_list, (viz::RasterContextProvider*)nullptr);
  }
  // 演示 SolidColorDrawQuad 的使用
  void AppendSolidColorDrawQuad(viz::CompositorFrame& frame,
                                viz::RenderPass* render_pass) {
    auto color =
        colors[(*frame_token_generator_ / 60 + 1) % base::size(colors)];
    TRACE_EVENT1("viz", "LayerTreeFrameSink::AppendSolidColorDrawQuad", "color",
                 color);
    gfx::Rect output_rect = bounds_;
    // Add a solid-color draw-quad for the big rectangle covering the entire
    // content-area of the client.
    viz::SharedQuadState* quad_state =
        render_pass->CreateAndAppendSharedQuadState();
    quad_state->SetAll(
        gfx::Transform(),
        /*quad_layer_rect=*/output_rect,
        /*visible_quad_layer_rect=*/output_rect,
        /*rounded_corner_bounds=*/gfx::RRectF(),
        /*clip_rect=*/gfx::Rect(),
        /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
        /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

    // 单一颜色
    viz::SolidColorDrawQuad* color_quad =
        render_pass->CreateAndAppendDrawQuad<viz::SolidColorDrawQuad>();
    color_quad->SetNew(quad_state, output_rect, output_rect, color, false);
  }
  viz::ResourceId CreateResource(const gfx::Size& size,
                                 const SkBitmap& source) {
    if (context_provider_ && g_use_gpu) {
      return CreateGpuResource(size, source);
    }
    return CreateSoftwareResource(size, source);
  }

  // 使用共享内存来传递资源到 viz
  viz::ResourceId CreateSoftwareResource(const gfx::Size& size,
                                         const SkBitmap& source) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::CreateSoftwareResource");
    viz::SharedBitmapId shared_bitmap_id = viz::SharedBitmap::GenerateId();
    // 创建共享内存
    base::MappedReadOnlyRegion shm =
        viz::bitmap_allocation::AllocateSharedBitmap(size, viz::RGBA_8888);
    base::WritableSharedMemoryMapping mapping = std::move(shm.mapping);

    SkImageInfo info = SkImageInfo::MakeN32Premul(size.width(), size.height());
    // 将 SkBitmap 中的像素数据拷贝到共享内存
    source.readPixels(info, mapping.memory(), info.minRowBytes(), 0, 0);

    // 将共享内存及与之对应的资源Id发送到 viz service 端
    frame_sink_remote_->DidAllocateSharedBitmap(std::move(shm.region),
                                                shared_bitmap_id);

    // 把资源存入 ClientResourceProvider 进行统一管理。
    // 后续会使用 ClientResourceProvider::PrepareSendToParent
    // 将已经存入的资源添加 到 CF 中。
    return client_resource_provider_->ImportResource(
        viz::TransferableResource::MakeSoftware(shared_bitmap_id, size,
                                                viz::RGBA_8888),
        viz::SingleReleaseCallback::Create(base::DoNothing()));
  }
  viz::ResourceId CreateGpuResource(const gfx::Size& size,
                                    const SkBitmap& source) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::CreateGpuResource");
    DCHECK(context_provider_);
    gpu::SharedImageInterface* sii = context_provider_->SharedImageInterface();
    DCHECK(sii);

    auto pixels =
        base::make_span(static_cast<const uint8_t*>(source.getPixels()),
                        source.computeByteSize());
    auto format = viz::RGBA_8888;
    auto color_space = gfx::ColorSpace();
    gpu::Mailbox mailbox = sii->CreateSharedImage(
        format, size, color_space, gpu::SHARED_IMAGE_USAGE_DISPLAY, pixels);
    gpu::SyncToken sync_token = sii->GenVerifiedSyncToken();

    viz::TransferableResource gl_resource = viz::TransferableResource::MakeGL(
        mailbox, GL_LINEAR, GL_TEXTURE_2D, sync_token, size,
        false /* is_overlay_candidate */);
    gl_resource.format = format;
    gl_resource.color_space = std::move(color_space);
    auto release_callback = viz::SingleReleaseCallback::Create(
        base::BindOnce(&InkClient::DeleteSharedImage, base::Unretained(this),
                       context_provider_, mailbox));

    return client_resource_provider_->ImportResource(
        gl_resource, std::move(release_callback));
  }

  void DeleteSharedImage(scoped_refptr<viz::ContextProvider> context_provider,
                         gpu::Mailbox mailbox,
                         const gpu::SyncToken& sync_token,
                         bool is_lost) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::DeleteSharedImage");
    DCHECK(context_provider);
    gpu::SharedImageInterface* sii = context_provider->SharedImageInterface();
    DCHECK(sii);
    sii->DestroySharedImage(sync_token, mailbox);
  }

  void OnBeginFrame(
      const ::viz::BeginFrameArgs& args,
      const base::flat_map<uint32_t, ::viz::FrameTimingDetails>& details)
      override {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::OnBeginFrame");
    if (need_redraw_) {
      frame_sink_remote_->SubmitCompositorFrame(
          local_surface_id_.local_surface_id(), CreateFrame(args),
          base::Optional<viz::HitTestRegionList>(),
          /*trace_time=*/0);
    } else {
      frame_sink_remote_->DidNotProduceFrame(viz::BeginFrameAck(args, false));
    }
    need_redraw_ = false;
    // frame_sink_remote_->SetNeedsBeginFrame(false);
  }

  void DidReceiveCompositorFrameAck(
      const std::vector<::viz::ReturnedResource>& resources) override {
    TRACE_EVENT1("viz", "LayerTreeFrameSink::DidReceiveCompositorFrameAck",
                 "size", resources.size());
    // DLOG(INFO) << __FUNCTION__;
    client_resource_provider_->ReceiveReturnsFromParent(resources);
    for (auto resource : resources) {
      client_resource_provider_->RemoveImportedResource(resource.id);
    }
  }

  void OnBeginFramePausedChanged(bool paused) override {
    // DLOG(INFO) << __FUNCTION__;
  }

  void ReclaimResources(
      const std::vector<::viz::ReturnedResource>& resources) override {
    TRACE_EVENT1("viz", "LayerTreeFrameSink::ReclaimResources", "size",
                 resources.size());
    // DLOG(INFO) << __FUNCTION__;
    client_resource_provider_->ReceiveReturnsFromParent(resources);
    for (auto resource : resources) {
      client_resource_provider_->RemoveImportedResource(resource.id);
    }
  }

  viz::FrameSinkId frame_sink_id_;
  viz::LocalSurfaceIdAllocation local_surface_id_;
  gfx::Rect bounds_;
  // 模拟每个 Client 都在独立的线程中生成 CF
  base::Thread thread_;

  mojo::Receiver<viz::mojom::CompositorFrameSinkClient> receiver_{this};
  mojo::Remote<viz::mojom::CompositorFrameSink> frame_sink_remote_;
  viz::ParentLocalSurfaceIdAllocator local_surface_id_allocator_;
  viz::FrameTokenGenerator frame_token_generator_;
  scoped_refptr<viz::ContextProvider> context_provider_;
  std::unique_ptr<SkBitmap> bitmap_;
  std::unique_ptr<SkCanvas> canvas_;
  SkPath path_;
  SkPaint paint_;
  bool need_redraw_;

  std::unique_ptr<viz::ClientResourceProvider> client_resource_provider_;
};

// Client 端
// 类似 Chromium 中的 *LayerTreeFrameSink 的作用.
class LayerTreeFrameSink : public viz::mojom::CompositorFrameSinkClient {
 public:
  LayerTreeFrameSink(const viz::FrameSinkId& frame_sink_id,
                     const viz::LocalSurfaceIdAllocation& local_surface_id,
                     const gfx::Rect& bounds)
      : frame_sink_id_(frame_sink_id),
        local_surface_id_(local_surface_id),
        bounds_(bounds),
        thread_("Demo_" + frame_sink_id.ToString()) {
    CHECK(thread_.Start());
  }

  ~LayerTreeFrameSink() override {}

  // remote 和 associated_remote 只能一个有效.
  // remote 用于非 root 的 client, associated_remote 用于 root client.
  // 这表示 root client 提交 CF 的过程需要和 FSM 的调用进行同步,而其他 client
  // 可以在单独的通信通道中提交 CF.
  void Bind(
      mojo::PendingReceiver<viz::mojom::CompositorFrameSinkClient> receiver,
      mojo::PendingAssociatedRemote<viz::mojom::CompositorFrameSink>
          associated_remote) {
    thread_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&LayerTreeFrameSink::BindOnThread,
                       base::Unretained(this), std::move(receiver),
                       std::move(associated_remote), mojo::NullRemote()));
  }

  void Bind(
      mojo::PendingReceiver<viz::mojom::CompositorFrameSinkClient> receiver,
      mojo::PendingRemote<viz::mojom::CompositorFrameSink> remote) {
    thread_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&LayerTreeFrameSink::BindOnThread,
                       base::Unretained(this), std::move(receiver),
                       mojo::NullAssociatedRemote(), std::move(remote)));
  }

  void SetContextProvider(
      scoped_refptr<viz::ContextProvider> context_provider) {
    if (thread_.task_runner()->BelongsToCurrentThread()) {
      LOG(INFO) << "SetContextProvider";
      context_provider_ = context_provider;
      DCHECK(context_provider->BindToCurrentThread() ==
             gpu::ContextResult::kSuccess);
    } else {
      thread_.task_runner()->PostTask(
          FROM_HERE, base::BindOnce(&LayerTreeFrameSink::SetContextProvider,
                                    base::Unretained(this), context_provider));
    }
  }

  viz::FrameSinkId frame_sink_id() { return frame_sink_id_; }

  viz::LocalSurfaceIdAllocation EmbedChild(
      const viz::FrameSinkId& child_frame_sink_id) {
    base::AutoLock lock(lock_);
    child_frame_sink_id_ = child_frame_sink_id;
    local_surface_id_allocator_.GenerateId();
    return local_surface_id_allocator_.GetCurrentLocalSurfaceIdAllocation();
  }

 private:
  void BindOnThread(
      mojo::PendingReceiver<viz::mojom::CompositorFrameSinkClient> receiver,
      mojo::PendingAssociatedRemote<viz::mojom::CompositorFrameSink>
          associated_remote,
      mojo::PendingRemote<viz::mojom::CompositorFrameSink> remote) {
    receiver_.Bind(std::move(receiver));
    if (associated_remote) {
      frame_sink_associated_remote_.Bind(std::move(associated_remote));
    } else {
      frame_sink_remote_.Bind(std::move(remote));
    }
    // 告诉 CompositorFrameSink 可以开始请求 CompositorFrame 了
    GetCompositorFrameSinkPtr()->SetNeedsBeginFrame(true);
    client_resource_provider_ =
        std::make_unique<viz::ClientResourceProvider>(false);
  }

  viz::CompositorFrame CreateFrame(const ::viz::BeginFrameArgs& args) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::CreateFrame");
    // LOG(INFO) << "CreateFrame: " << frame_sink_id_ << ":" <<
    // local_surface_id_;

    viz::CompositorFrame frame;
    frame.metadata.begin_frame_ack = viz::BeginFrameAck(args, true);
    frame.metadata.device_scale_factor = 1.f;
    frame.metadata.local_surface_id_allocation_time =
        local_surface_id_.allocation_time();
    frame.metadata.frame_token = *frame_token_generator_;
    frame.metadata.send_frame_token_to_embedder = true;

    const int kRenderPassId = 1;
    const gfx::Rect& output_rect = bounds_;
    const gfx::Rect& damage_rect = output_rect;
    std::unique_ptr<viz::RenderPass> render_pass = viz::RenderPass::Create();
    render_pass->SetNew(kRenderPassId, output_rect, damage_rect,
                        gfx::Transform());

    AppendDebugBorderDrawQuad(frame, render_pass.get());

    if (child_frame_sink_id_.is_valid()) {
      AppendSurfaceDrawQuad(frame, render_pass.get());
    }
    if (context_provider_) {
      AppendTileDrawQuad(frame, render_pass.get());
      AppendTextureDrawQuad(frame, render_pass.get());
      AppendPictureDrawQuad(frame, render_pass.get());
      AppendVideoHoleDrawQuad(frame, render_pass.get());
    }
    AppendSolidColorDrawQuad(frame, render_pass.get());

    // SoftwareOutputDeviceX11 不支持离屏渲染
    if (g_use_gpu) {
      // 使用Bitmap方式获取该 render_pass 渲染的结果，
      // Texture 方式已经在2020.7.23日被移除，详见：
      // https://bugs.chromium.org/p/chromium/issues/detail?id=1044594
      auto request = std::make_unique<viz::CopyOutputRequest>(
          viz::CopyOutputResult::Format::RGBA_BITMAP,  // RGBA_TEXTURE
          base::BindOnce(&LayerTreeFrameSink::OnGetOutputResult,
                         base::Unretained(this)));
      request->set_result_task_runner(base::SequencedTaskRunnerHandle::Get());
      render_pass->copy_requests.push_back(std::move(request));
    }

    frame.render_pass_list.push_back(std::move(render_pass));

    return frame;
  }

  // 这里也可以改为渲染在其他窗口中，但是无法达到60fps
  void OnGetOutputResult(std::unique_ptr<viz::CopyOutputResult> result) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::OnGetOutputResult");
    DCHECK(!result->IsEmpty());
    // 保存渲染结果到图片文件
    constexpr char filename[] = "result_demo_viz_layer.png";
    base::FilePath path;
    DCHECK(base::PathService::Get(base::BasePathKey::DIR_EXE, &path));
    path = path.AppendASCII(filename);

    SkFILEWStream stream(path.value().c_str());
    DCHECK(SkEncodeImage(&stream, result->AsSkBitmap().pixmap(),
                         SkEncodedImageFormat::kPNG, 0));
    DLOG(INFO) << "OnGetOutputResult: save the frame to: " << path;
  }

  void AppendDebugBorderDrawQuad(viz::CompositorFrame& frame,
                                 viz::RenderPass* render_pass) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::AppendDebugBorderDrawQuad");
    gfx::Rect output_rect = bounds_;

    auto* quad_state = render_pass->CreateAndAppendSharedQuadState();
    quad_state->SetAll(
        gfx::Transform(),
        /*quad_layer_rect=*/output_rect,
        /*visible_quad_layer_rect=*/output_rect,
        /*rounded_corner_bounds=*/gfx::RRectF(),
        /*clip_rect=*/output_rect,
        /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
        /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

    auto* debug_quad =
        render_pass->CreateAndAppendDrawQuad<viz::DebugBorderDrawQuad>();
    // 将 resource 添加到 tile_quad 中
    debug_quad->SetNew(quad_state, output_rect, output_rect, SK_ColorMAGENTA,
                       20);
  }

  // 演示 TileDrawQuad 的使用
  void AppendTileDrawQuad(viz::CompositorFrame& frame,
                          viz::RenderPass* render_pass) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::AppendTileDrawQuad");
    // LOG(INFO) << "AppendTileDrawQuad: " << frame_sink_id_ << ":" <<
    // local_surface_id_; 创建要渲染的内容到 SkBitmap 中
    SkBitmap bitmap;
    bitmap.allocPixels(SkImageInfo::Make(200, 200, kRGBA_8888_SkColorType,
                                     kOpaque_SkAlphaType));
    SkCanvas canvas(bitmap);
    canvas.clear(SK_ColorWHITE);
    canvas.drawCircle(
        30, 100, 150,
        SkPaint(SkColor4f::FromColor(
            colors[(*frame_token_generator_ / 60 + 1) % base::size(colors)])));
    canvas.drawCircle(
        10, 50, 60,
        SkPaint(SkColor4f::FromColor(
            colors[(*frame_token_generator_ / 60 + 2) % base::size(colors)])));
    canvas.drawCircle(
        180, 180, 50,
        SkPaint(SkColor4f::FromColor(
            colors[(*frame_token_generator_ / 60 + 3) % base::size(colors)])));

    gfx::Size tile_size(200, 200);
    // 将 SkBitmap 中的数据转换为资源
    viz::ResourceId resource = CreateResource(tile_size, bitmap);

    gfx::Rect output_rect{0, 0, 200, 200};
    gfx::Transform transform;
    transform.Translate(50, 50);

    auto* quad_state = render_pass->CreateAndAppendSharedQuadState();
    quad_state->SetAll(
        transform,
        /*quad_layer_rect=*/output_rect,
        /*visible_quad_layer_rect=*/output_rect,
        /*rounded_corner_bounds=*/gfx::RRectF(),
        /*clip_rect=*/output_rect,
        /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
        /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

    auto* tile_quad = render_pass->CreateAndAppendDrawQuad<viz::TileDrawQuad>();
    // 将 resource 添加到 tile_quad 中
    tile_quad->SetNew(quad_state, output_rect, output_rect, false, resource,
                      gfx::RectF(output_rect), output_rect.size(), true, true,
                      true);

    // 将 resource 对用的资源添加到 frame.resource_list
    // 中，在最简单的情况下可以直接使用 frame.resource_list.push_back(...)
    // 来添加
    client_resource_provider_->PrepareSendToParent(
        {resource}, &frame.resource_list, (viz::RasterContextProvider*)nullptr);
  }

  // 演示 TextureDrawQuad 的使用
  void AppendTextureDrawQuad(viz::CompositorFrame& frame,
                             viz::RenderPass* render_pass) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::AppendTextureDrawQuad");
    // 创建要渲染的内容到 SkBitmap 中
    SkBitmap bitmap;
    bitmap.allocPixels(SkImageInfo::Make(200, 200, kRGBA_8888_SkColorType,
                                     kOpaque_SkAlphaType));
    SkCanvas canvas(bitmap);
    canvas.clear(SK_ColorWHITE);
    canvas.drawCircle(
        30, 100, 150,
        SkPaint(SkColor4f::FromColor(
            colors[(*frame_token_generator_ / 60 + 2) % base::size(colors)])));
    canvas.drawCircle(
        10, 50, 60,
        SkPaint(SkColor4f::FromColor(
            colors[(*frame_token_generator_ / 60 + 3) % base::size(colors)])));
    canvas.drawCircle(
        180, 180, 50,
        SkPaint(SkColor4f::FromColor(
            colors[(*frame_token_generator_ / 60 + 1) % base::size(colors)])));

    gfx::Size tile_size(200, 200);
    // 将 SkBitmap 中的数据转换为资源
    viz::ResourceId resource = CreateResource(tile_size, bitmap);

    gfx::Rect output_rect{0, 0, 200, 200};
    gfx::Transform transform;
    transform.Translate(350, 50);

    auto* quad_state = render_pass->CreateAndAppendSharedQuadState();
    quad_state->SetAll(
        transform,
        /*quad_layer_rect=*/output_rect,
        /*visible_quad_layer_rect=*/output_rect,
        /*rounded_corner_bounds=*/gfx::RRectF(),
        /*clip_rect=*/output_rect,
        /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
        /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

    auto* texture_quad =
        render_pass->CreateAndAppendDrawQuad<viz::TextureDrawQuad>();
    float vertex_opacity[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // 将 resource 添加到 tile_quad 中
    texture_quad->SetNew(quad_state, output_rect, output_rect, false, resource,
                         true, gfx::PointF(0.f, 0.f), gfx::PointF(1.f, 1.f),
                         SK_ColorGRAY, vertex_opacity, false, false, false,
                         gfx::ProtectedVideoType::kClear);

    // 将 resource 对用的资源添加到 frame.resource_list
    // 中，在最简单的情况下可以直接使用 frame.resource_list.push_back(...)
    // 来添加
    client_resource_provider_->PrepareSendToParent(
        {resource}, &frame.resource_list, (viz::RasterContextProvider*)nullptr);
  }

  void AppendSurfaceDrawQuad(viz::CompositorFrame& frame,
                             viz::RenderPass* render_pass) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::AppendSurfaceDrawQuad");
    gfx::Rect output_rect{0, 0, 200, 200};
    gfx::Transform transform;
    transform.Translate(350, 350);

    auto* quad_state = render_pass->CreateAndAppendSharedQuadState();
    quad_state->SetAll(
        transform,
        /*quad_layer_rect=*/output_rect,
        /*visible_quad_layer_rect=*/output_rect,
        /*rounded_corner_bounds=*/gfx::RRectF(),
        /*clip_rect=*/output_rect,
        /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
        /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

    viz::SurfaceId child_surface_id(
        child_frame_sink_id_,
        local_surface_id_allocator_.GetCurrentLocalSurfaceIdAllocation()
            .local_surface_id());
    auto* surface_quad =
        render_pass->CreateAndAppendDrawQuad<viz::SurfaceDrawQuad>();
    surface_quad->SetNew(quad_state, output_rect, output_rect,
                         viz::SurfaceRange(base::nullopt, child_surface_id),
                         SK_ColorDKGRAY, true);
  }

  // 演示 TextureDrawQuad 的使用
  // VideoHoleDrawQuad 需要 viz overlay 机制支持，在Linux上还不支持，会显示为
  // SK_ColorMAGENTA 色块
  // TODO: 研究 viz overlay 机制
  void AppendVideoHoleDrawQuad(viz::CompositorFrame& frame,
                               viz::RenderPass* render_pass) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::AppendVideoHoleDrawQuad");
    gfx::Rect output_rect{0, 0, 200, 200};
    gfx::Transform transform;
    transform.Translate(50, 350);

    auto* quad_state = render_pass->CreateAndAppendSharedQuadState();
    quad_state->SetAll(
        transform,
        /*quad_layer_rect=*/output_rect,
        /*visible_quad_layer_rect=*/output_rect,
        /*rounded_corner_bounds=*/gfx::RRectF(),
        /*clip_rect=*/output_rect,
        /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
        /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

    auto* video_hole_quad =
        render_pass->CreateAndAppendDrawQuad<viz::VideoHoleDrawQuad>();

    static const base::UnguessableToken overlay_plane_id =
        base::UnguessableToken::Create();
    video_hole_quad->SetNew(quad_state, output_rect, output_rect,
                            overlay_plane_id);
  }

  // 演示 PictureDrawQuad 的使用
  // PictureDrawQuad 当前不支持 mojo 的序列化，因此这里无法演示
  void AppendPictureDrawQuad(viz::CompositorFrame& frame,
                             viz::RenderPass* render_pass) {
    return;
    gfx::Rect output_rect = bounds_;
    output_rect.Inset(10, 10, 10, 10);

    auto display_list = base::MakeRefCounted<cc::DisplayItemList>();
    display_list->StartPaint();
    display_list->push<cc::DrawColorOp>(SK_ColorCYAN, SkBlendMode::kSrc);
    display_list->EndPaintOfUnpaired(output_rect);
    display_list->Finalize();

    viz::SharedQuadState* quad_state =
        render_pass->CreateAndAppendSharedQuadState();
    quad_state->SetAll(
        gfx::Transform(),
        /*quad_layer_rect=*/output_rect,
        /*visible_quad_layer_rect=*/output_rect,
        /*rounded_corner_bounds=*/gfx::RRectF(),
        /*clip_rect=*/gfx::Rect(),
        /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
        /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

    auto* picture_quad =
        render_pass->CreateAndAppendDrawQuad<viz::PictureDrawQuad>();
    picture_quad->SetNew(quad_state, output_rect, output_rect, true,
                         gfx::RectF(output_rect), output_rect.size(), false,
                         viz::RGBA_8888, output_rect, 1.f, {}, display_list);
  }

  // 演示 SolidColorDrawQuad 的使用
  void AppendSolidColorDrawQuad(viz::CompositorFrame& frame,
                                viz::RenderPass* render_pass) {
    auto color = colors[(*frame_token_generator_ / 60) % base::size(colors)];
    TRACE_EVENT1("viz", "LayerTreeFrameSink::AppendSolidColorDrawQuad", "color",
                 color);
    gfx::Rect output_rect = bounds_;
    // Add a solid-color draw-quad for the big rectangle covering the entire
    // content-area of the client.
    viz::SharedQuadState* quad_state =
        render_pass->CreateAndAppendSharedQuadState();
    quad_state->SetAll(
        gfx::Transform(),
        /*quad_layer_rect=*/output_rect,
        /*visible_quad_layer_rect=*/output_rect,
        /*rounded_corner_bounds=*/gfx::RRectF(),
        /*clip_rect=*/gfx::Rect(),
        /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
        /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

    // 单一颜色
    viz::SolidColorDrawQuad* color_quad =
        render_pass->CreateAndAppendDrawQuad<viz::SolidColorDrawQuad>();
    color_quad->SetNew(
        quad_state, output_rect, output_rect,
        colors[(*frame_token_generator_ / 60) % base::size(colors)], false);
  }

  viz::ResourceId CreateResource(const gfx::Size& size,
                                 const SkBitmap& source) {
    if (context_provider_ && g_use_gpu) {
      return CreateGpuResource(size, source);
    }
    return CreateSoftwareResource(size, source);
  }

  // 使用共享内存来传递资源到 viz
  viz::ResourceId CreateSoftwareResource(const gfx::Size& size,
                                         const SkBitmap& source) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::CreateSoftwareResource");
    viz::SharedBitmapId shared_bitmap_id = viz::SharedBitmap::GenerateId();
    // 创建共享内存
    base::MappedReadOnlyRegion shm =
        viz::bitmap_allocation::AllocateSharedBitmap(size, viz::RGBA_8888);
    base::WritableSharedMemoryMapping mapping = std::move(shm.mapping);

    SkImageInfo info = SkImageInfo::MakeN32Premul(size.width(), size.height());
    // 将 SkBitmap 中的像素数据拷贝到共享内存
    source.readPixels(info, mapping.memory(), info.minRowBytes(), 0, 0);

    // 将共享内存及与之对应的资源Id发送到 viz service 端
    GetCompositorFrameSinkPtr()->DidAllocateSharedBitmap(std::move(shm.region),
                                                         shared_bitmap_id);

    // 把资源存入 ClientResourceProvider 进行统一管理。
    // 后续会使用 ClientResourceProvider::PrepareSendToParent
    // 将已经存入的资源添加 到 CF 中。
    return client_resource_provider_->ImportResource(
        viz::TransferableResource::MakeSoftware(shared_bitmap_id, size,
                                                viz::RGBA_8888),
        viz::SingleReleaseCallback::Create(base::DoNothing()));
  }

  viz::ResourceId CreateGpuResource(const gfx::Size& size,
                                    const SkBitmap& source) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::CreateGpuResource");
    DCHECK(context_provider_);
    gpu::SharedImageInterface* sii = context_provider_->SharedImageInterface();
    DCHECK(sii);
    auto pixels =
        base::make_span(static_cast<const uint8_t*>(source.getPixels()),
                        source.computeByteSize());
    auto format = viz::RGBA_8888;
    auto color_space = gfx::ColorSpace();
    // 这里直接使用Raster的结果(pixels)创建一个SharedImage
    // 也可以使用 CHROMIUM_raster_transport 扩展进行 OOP-R Raster
    // 或者使用 CHROMIUM_shared_image 扩展进行 OOP-D Raster
    // OOP-D 参考 https://source.chromium.org/chromium/chromium/src/+/master:cc/raster/gpu_raster_buffer_provider.cc;l=119;
    // OOP-R 参考 https://source.chromium.org/chromium/chromium/src/+/master:cc/raster/gpu_raster_buffer_provider.cc;l=173;
    gpu::Mailbox mailbox = sii->CreateSharedImage(
        format, size, color_space, gpu::SHARED_IMAGE_USAGE_DISPLAY, pixels);
    gpu::SyncToken sync_token = sii->GenVerifiedSyncToken();

    viz::TransferableResource gl_resource = viz::TransferableResource::MakeGL(
        mailbox, GL_LINEAR, GL_TEXTURE_2D, sync_token, size,
        false /* is_overlay_candidate */);
    gl_resource.format = format;
    gl_resource.color_space = std::move(color_space);
    auto release_callback = viz::SingleReleaseCallback::Create(
        base::BindOnce(&LayerTreeFrameSink::DeleteSharedImage,
                       base::Unretained(this), context_provider_, mailbox));
    return client_resource_provider_->ImportResource(
        gl_resource, std::move(release_callback));
  }

  void DeleteSharedImage(scoped_refptr<viz::ContextProvider> context_provider,
                         gpu::Mailbox mailbox,
                         const gpu::SyncToken& sync_token,
                         bool is_lost) {
    TRACE_EVENT0("viz", "LayerTreeFrameSink::DeleteSharedImage");
    DCHECK(context_provider);
    gpu::SharedImageInterface* sii = context_provider->SharedImageInterface();
    DCHECK(sii);
    sii->DestroySharedImage(sync_token, mailbox);
  }

  void DidReceiveCompositorFrameAck(
      const std::vector<::viz::ReturnedResource>& resources) override {
    TRACE_EVENT1("viz", "LayerTreeFrameSink::DidReceiveCompositorFrameAck",
                 "size", resources.size());
    // DLOG(INFO) << __FUNCTION__;
    client_resource_provider_->ReceiveReturnsFromParent(resources);
    for (auto resource : resources) {
      client_resource_provider_->RemoveImportedResource(resource.id);
    }
  }

  void OnBeginFrame(
      const ::viz::BeginFrameArgs& args,
      const base::flat_map<uint32_t, ::viz::FrameTimingDetails>& details)
      override {
    base::AutoLock lock(lock_);
    // 每 60fps 刷新一帧
    if (++frame_token_generator_ % 60 == 1) {
      GetCompositorFrameSinkPtr()->SubmitCompositorFrame(
          local_surface_id_.local_surface_id(), CreateFrame(args),
          base::Optional<viz::HitTestRegionList>(),
          /*trace_time=*/0);
    } else {
      GetCompositorFrameSinkPtr()->DidNotProduceFrame(
          viz::BeginFrameAck(args, false));
    }
  }

  void OnBeginFramePausedChanged(bool paused) override {
    DLOG(INFO) << __FUNCTION__;
  }

  void ReclaimResources(
      const std::vector<::viz::ReturnedResource>& resources) override {
    TRACE_EVENT1("viz", "LayerTreeFrameSink::ReclaimResources", "size",
                 resources.size());
    // DLOG(INFO) << __FUNCTION__;
    client_resource_provider_->ReceiveReturnsFromParent(resources);
    for (auto resource : resources) {
      client_resource_provider_->RemoveImportedResource(resource.id);
    }
  }

  viz::mojom::CompositorFrameSink* GetCompositorFrameSinkPtr() {
    if (frame_sink_associated_remote_.is_bound())
      return frame_sink_associated_remote_.get();
    return frame_sink_remote_.get();
  }

  mojo::Receiver<viz::mojom::CompositorFrameSinkClient> receiver_{this};
  mojo::AssociatedRemote<viz::mojom::CompositorFrameSink>
      frame_sink_associated_remote_;
  mojo::Remote<viz::mojom::CompositorFrameSink> frame_sink_remote_;
  viz::FrameSinkId frame_sink_id_;
  viz::LocalSurfaceIdAllocation local_surface_id_;
  gfx::Rect bounds_;
  viz::ParentLocalSurfaceIdAllocator local_surface_id_allocator_;
  viz::FrameSinkId child_frame_sink_id_;
  // 模拟每个 Client 都在独立的线程中生成 CF
  base::Thread thread_;
  viz::FrameTokenGenerator frame_token_generator_;
  base::Lock lock_;
  scoped_refptr<viz::ContextProvider> context_provider_;

  std::unique_ptr<viz::ClientResourceProvider> client_resource_provider_;
};

// Host 端
// 在 Chromium 中，Compositor 实现了 HostFrameSinkClient 接口，这里模拟 Chromium
// 中的命名。
class Compositor : public viz::HostFrameSinkClient {
 public:
  Compositor(gfx::AcceleratedWidget widget,
             gfx::Size size,
             mojo::PendingReceiver<viz::mojom::FrameSinkManagerClient> client,
             mojo::PendingRemote<viz::mojom::FrameSinkManager> manager)
      : widget_(widget),
        size_(size) /*, compositor_thread_("CompositorThread")*/ {
    // CHECK(compositor_thread_.Start());
    // compositor_thread_.task_runner()->PostTask(
    //     FROM_HERE,
    //     base::BindOnce(&Compositor::InitializeOnThread,
    //     base::Unretained(this),
    //                    std::move(client), std::move(manager)));
    InitializeOnThread(std::move(client), std::move(manager));
  }

  void SetContextProvider(
      scoped_refptr<viz::ContextProvider> root_context_provider,
      scoped_refptr<viz::ContextProvider> child_context_provider) {
    root_client_->SetContextProvider(root_context_provider);
    child_client_->SetContextProvider(child_context_provider);
  }

  void Resize(gfx::Size size) {
    // TODO:
  }

  gfx::AcceleratedWidget widget() { return widget_; }

  // Called when a CompositorFrame with a new SurfaceId activates for the first
  // time.
  void OnFirstSurfaceActivation(
      const viz::SurfaceInfo& surface_info) override {
    DLOG(INFO) << __FUNCTION__;
  }

  // Called when a CompositorFrame with a new frame token is provided.
  void OnFrameTokenChanged(uint32_t frame_token) override {
    TRACE_EVENT0("viz", "Compositor::OnFrameTokenChanged");
    // DLOG(INFO) << __FUNCTION__;
  }

 private:
  void InitializeOnThread(
      mojo::PendingReceiver<viz::mojom::FrameSinkManagerClient> client,
      mojo::PendingRemote<viz::mojom::FrameSinkManager> manager) {
    host_frame_sink_manager_.BindAndSetManager(std::move(client), nullptr,
                                               std::move(manager));
    display_client_ = std::make_unique<viz::HostDisplayClient>(widget_);

    // 创建 root client 的 FrameSinkId
    viz::FrameSinkId root_frame_sink_id =
        frame_sink_id_allocator_.NextFrameSinkId();

    // 注册 root client 的 FrameSinkId
    host_frame_sink_manager_.RegisterFrameSinkId(
        root_frame_sink_id, this, viz::ReportFirstSurfaceActivation::kNo);

    mojo::PendingAssociatedRemote<viz::mojom::CompositorFrameSink>
        frame_sink_remote;
    auto frame_sink_receiver =
        frame_sink_remote.InitWithNewEndpointAndPassReceiver();

    mojo::PendingRemote<viz::mojom::CompositorFrameSinkClient>
        root_client_remote;
    mojo::PendingReceiver<viz::mojom::CompositorFrameSinkClient>
        root_client_receiver =
            root_client_remote.InitWithNewPipeAndPassReceiver();

    auto params = viz::mojom::RootCompositorFrameSinkParams::New();
    params->widget = widget_;
    params->compositor_frame_sink = std::move(frame_sink_receiver);
    params->compositor_frame_sink_client = std::move(root_client_remote);
    params->frame_sink_id = root_frame_sink_id;
    params->disable_frame_rate_limit =
        base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kDisableFrameRateLimit);
    params->gpu_compositing = g_use_gpu;
    // 只有Andorid平台能使用
    // params->refresh_rate = 1.0f;
    params->display_client = display_client_->GetBoundRemote(nullptr);
    params->display_private = display_private_.BindNewEndpointAndPassReceiver();
    // CreateRendererSettings 里面有很多和渲染相关的设置,有些对于调试非常方便
    params->renderer_settings = viz::CreateRendererSettings();
    host_frame_sink_manager_.CreateRootCompositorFrameSink(std::move(params));

    display_private_->Resize(size_);
    display_private_->SetDisplayVisible(true);

    local_surface_id_allocator_.GenerateId();
    root_client_ = std::make_unique<LayerTreeFrameSink>(
        root_frame_sink_id,
        local_surface_id_allocator_.GetCurrentLocalSurfaceIdAllocation(),
        gfx::Rect(size_));
    root_client_->Bind(std::move(root_client_receiver),
                       std::move(frame_sink_remote));
    EmbedChildClient(root_frame_sink_id);
  }

  void EmbedChildClient(viz::FrameSinkId parent_frame_sink_id) {
    // 创建 child 的 FrameSinkId
    viz::FrameSinkId frame_sink_id = frame_sink_id_allocator_.NextFrameSinkId();
    // uint64_t rand = base::RandUint64();
    // viz::FrameSinkId frame_sink_id(rand >> 32, rand & 0xffffffff);

    // 注册 child client 的 FrameSinkId
    host_frame_sink_manager_.RegisterFrameSinkId(
        frame_sink_id, this, viz::ReportFirstSurfaceActivation::kNo);
    host_frame_sink_manager_.RegisterFrameSinkHierarchy(parent_frame_sink_id,
                                                        frame_sink_id);

    mojo::PendingRemote<viz::mojom::CompositorFrameSink> frame_sink_remote;
    auto frame_sink_receiver =
        frame_sink_remote.InitWithNewPipeAndPassReceiver();

    mojo::PendingRemote<viz::mojom::CompositorFrameSinkClient> client_remote;
    mojo::PendingReceiver<viz::mojom::CompositorFrameSinkClient>
        client_receiver = client_remote.InitWithNewPipeAndPassReceiver();
    host_frame_sink_manager_.CreateCompositorFrameSink(
        frame_sink_id, std::move(frame_sink_receiver),
        std::move(client_remote));

    auto child_local_surface_id = root_client_->EmbedChild(frame_sink_id);
    child_client_ = std::make_unique<InkClient>(
        frame_sink_id, child_local_surface_id, gfx::Rect(size_));
    child_client_->Bind(std::move(client_receiver),
                        std::move(frame_sink_remote));
  }

  gfx::AcceleratedWidget widget_;
  gfx::Size size_;
  viz::HostFrameSinkManager host_frame_sink_manager_;
  // base::Thread compositor_thread_;
  viz::FrameSinkIdAllocator frame_sink_id_allocator_{0};
  viz::ParentLocalSurfaceIdAllocator local_surface_id_allocator_;
  std::unique_ptr<viz::HostDisplayClient> display_client_;
  mojo::AssociatedRemote<viz::mojom::DisplayPrivate> display_private_;
  std::unique_ptr<LayerTreeFrameSink> root_client_;
  std::unique_ptr<InkClient> child_client_;
  scoped_refptr<viz::ContextProvider> main_context_provider_;
};

// Service 端
// 在 Chromium 中它运行于 GPU 进程,因此这里取名 GpuService
class GpuService : public viz::GpuHostImpl::Delegate,
                   public viz::VizMainImpl::Delegate,
                   public IPC::Listener {
 public:
  GpuService(mojo::PendingReceiver<viz::mojom::FrameSinkManager> receiver,
             mojo::PendingRemote<viz::mojom::FrameSinkManagerClient> client,
             Compositor* compositor)
      : host_io_thread_("Demo_HostIOThread"),
        gpu_io_thread_("Demo_GpuIOThread"),
        gpu_main_thread_("Demo_GpuMainThread"),
        compositor_(compositor),
        shutdown_event_(base::WaitableEvent::ResetPolicy::MANUAL,
                        base::WaitableEvent::InitialState::NOT_SIGNALED) {
    DCHECK(host_io_thread_.Start());
    DCHECK(gpu_io_thread_.Start());
    DCHECK(gpu_main_thread_.Start());
    host_main_thread_runner_ = base::ThreadTaskRunnerHandle::Get();
    host_io_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&GpuService::InitIPCServer, base::Unretained(this)));
    gpu_main_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&GpuService::InitVizMain, base::Unretained(this)));
    host_io_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&GpuService::InitVizHost, base::Unretained(this),
                       base::Passed(std::move(receiver)),
                       base::Passed(std::move(client))));
    // InitVizHost(std::move(receiver), std::move(client));
  }

  void InitIPCServer() {
    mojo::PendingRemote<IPC::mojom::ChannelBootstrap> bootstrap;
    auto bootstrap_receiver = bootstrap.InitWithNewPipeAndPassReceiver();

    server_channel_ = IPC::ChannelMojo::Create(
        bootstrap.PassPipe(), IPC::Channel::MODE_SERVER, this,
        base::ThreadTaskRunnerHandle::Get(),
        base::ThreadTaskRunnerHandle::Get(),
        mojo::internal::MessageQuotaChecker::MaybeCreate());

#if BUILDFLAG(IPC_MESSAGE_LOG_ENABLED)
    // We must make sure to instantiate the IPC Logger *before* we create the
    // channel, otherwise we can get a callback on the IO thread which creates
    // the logger, and the logger does not like being created on the IO
    // thread.
    IPC::Logging::GetInstance();
#endif

    gpu_main_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&GpuService::InitIPCClient, base::Unretained(this),
                       base::Passed(std::move(bootstrap_receiver))));

    DCHECK(server_channel_->Connect());
  }

  void InitIPCClient(
      mojo::PendingReceiver<IPC::mojom::ChannelBootstrap> bootstrap_receiver) {
    mojo::PendingRemote<IPC::mojom::ChannelBootstrap> legacy_ipc_bootstrap;
    mojo::ScopedMessagePipeHandle legacy_ipc_channel_handle =
        legacy_ipc_bootstrap.InitWithNewPipeAndPassReceiver().PassPipe();
    mojo::FusePipes(std::move(bootstrap_receiver),
                    std::move(legacy_ipc_bootstrap));
    client_channel_ =
        IPC::SyncChannel::Create(this, gpu_io_thread_.task_runner(),
                                 base::ThreadTaskRunnerHandle::Get(), nullptr);
    // #if BUILDFLAG(IPC_MESSAGE_LOG_ENABLED)
    //     if (!IsInBrowserProcess())
    //       IPC::Logging::GetInstance()->SetIPCSender(this);
    // #endif

    client_channel_->Init(
        IPC::ChannelMojo::CreateClientFactory(
            std::move(legacy_ipc_channel_handle), gpu_io_thread_.task_runner(),
            base::ThreadTaskRunnerHandle::Get()),
        /*create_pipe_now=*/true);
  }

  void InitVizHost(
      mojo::PendingReceiver<viz::mojom::FrameSinkManager> receiver,
      mojo::PendingRemote<viz::mojom::FrameSinkManagerClient> client) {
    mojo::PendingAssociatedRemote<viz::mojom::VizMain> viz_main_pending_remote;
    server_channel_->GetAssociatedInterfaceSupport()
        ->GetRemoteAssociatedInterface(
            viz_main_pending_remote.InitWithNewEndpointAndPassReceiver());

    viz::GpuHostImpl::InitFontRenderParams(
        gfx::GetFontRenderParams(gfx::FontRenderParamsQuery(), nullptr));
    factory_instance_ = new gpu::ShaderCacheFactory();

    viz::GpuHostImpl::InitParams params;
    params.restart_id = 1;
    params.disable_gpu_shader_disk_cache =
        base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kDisableGpuShaderDiskCache);
    params.product = "demo";
    params.deadline_to_synchronize_surfaces =
        switches::GetDeadlineToSynchronizeSurfaces();
    params.main_thread_task_runner = base::ThreadTaskRunnerHandle::Get();
    gpu_host_ = std::make_unique<viz::GpuHostImpl>(
        this, std::move(viz_main_pending_remote), std::move(params));
    gpu_host_->SetProcessId(base::GetCurrentProcId());
    gpu_host_->ConnectFrameSinkManager(std::move(receiver), std::move(client));
    gpu_client_id_ = g_unique_id.GetNext() + 1;
    gpu_host_->EstablishGpuChannel(
        gpu_client_id_, memory_instrumentation::mojom::kServiceTracingProcessId,
        true,
        base::BindOnce(&GpuService::OnEstablishedOnIO, base::Unretained(this)));
  }

  void InitVizMain() {
    auto gpu_init = std::make_unique<gpu::GpuInit>();
    gpu_init->InitializeInProcess(base::CommandLine::ForCurrentProcess(),
                                  GetGpuPreferencesFromCommandLine());
    viz_main_ = std::make_unique<viz::VizMainImpl>(
        this, CreateVizMainDependencies(), std::move(gpu_init));
    viz_main_->gpu_service()->set_start_time(base::Time::Now());
  }

 private:
  void OnEstablishedOnIO(mojo::ScopedMessagePipeHandle channel_handle,
                         const gpu::GPUInfo& gpu_info,
                         const gpu::GpuFeatureInfo& gpu_feature_info,
                         viz::GpuHostImpl::EstablishChannelStatus status) {
    if (channel_handle.is_valid()) {
      gpu_channel_host_ = base::MakeRefCounted<gpu::GpuChannelHost>(
          gpu_client_id_, gpu_info, gpu_feature_info,
          std::move(channel_handle));
    }
    host_main_thread_runner_->PostTask(
        FROM_HERE, base::BindOnce(&GpuService::OnEstablishedOnMain,
                                  base::Unretained(this)));
  }

  void OnEstablishedOnMain() {
    gpu_memory_buffer_manager_ =
        std::make_unique<viz::HostGpuMemoryBufferManager>(
            base::BindRepeating(&GpuService::GetGpuService,
                                base::Unretained(this)),
            gpu_client_id_, std::make_unique<gpu::GpuMemoryBufferSupport>(),
            host_main_thread_runner_);
    auto root_context_provider = CreateContextProvider(
        gpu::kNullSurfaceHandle, true,
        viz::command_buffer_metrics::ContextType::BROWSER_MAIN_THREAD);
    auto child_context_provider = CreateContextProvider(
        gpu::kNullSurfaceHandle, true,
        viz::command_buffer_metrics::ContextType::BROWSER_WORKER);
    compositor_->SetContextProvider(
        root_context_provider, child_context_provider);
  }

  gpu::GpuMemoryBufferManager* GetGpuMemoryBufferManager() {
    return gpu_memory_buffer_manager_.get();
  }

  viz::mojom::GpuService* GetGpuService(
      base::OnceClosure connection_error_handler) {
    gpu_host_->AddConnectionErrorHandler(std::move(connection_error_handler));
    return gpu_host_->gpu_service();
  }

  // 创建 ContextProvider ，它们都位于相同 stream
  scoped_refptr<viz::ContextProviderCommandBuffer> CreateContextProvider(
      gpu::SurfaceHandle handle,
      bool enable_oopr,
      viz::command_buffer_metrics::ContextType type) {
    constexpr bool kAutomaticFlushes = false;

    gpu::ContextCreationAttribs attributes;
    attributes.alpha_size = -1;
    attributes.depth_size = 0;
    attributes.stencil_size = 0;
    attributes.samples = 0;
    attributes.sample_buffers = 0;
    attributes.bind_generates_resource = false;
    attributes.lose_context_when_out_of_memory = true;
    attributes.buffer_preserved = false;
    attributes.enable_gles2_interface = true;
    attributes.enable_raster_interface = true;
    attributes.enable_oop_rasterization = enable_oopr;

    gpu::SharedMemoryLimits memory_limits =
        gpu::SharedMemoryLimits::ForDisplayCompositor();

    GURL url("demo://gpu/GpuService::CreateContextProvider");
    return base::MakeRefCounted<viz::ContextProviderCommandBuffer>(
        gpu_channel_host_, gpu_memory_buffer_manager_.get(), /*stream_id*/ 0,
        gpu::SchedulingPriority::kHigh, handle, std::move(url),
        kAutomaticFlushes, true, true, memory_limits, attributes, type);
  }

  viz::VizMainImpl::ExternalDependencies CreateVizMainDependencies() {
    viz::VizMainImpl::ExternalDependencies deps;
    deps.create_display_compositor = true;
    // if (!base::PowerMonitor::IsInitialized()) {
    //   deps.power_monitor_source =
    //       std::make_unique<base::PowerMonitorDeviceSource>();
    // }
    // if (GetContentClient()->gpu()) {
    // deps.sync_point_manager =
    // GetContentClient()->gpu()->GetSyncPointManager();
    // deps.shared_image_manager =
    //     GetContentClient()->gpu()->GetSharedImageManager();
    // deps.viz_compositor_thread_runner = runner_.get();
    // }
    // deps.shutdown_event = &shutdown_event_;
    deps.io_thread_task_runner = gpu_io_thread_.task_runner();

    // mojo::PendingRemote<ukm::mojom::UkmRecorderInterface> ukm_recorder;
    // ChildThread::Get()->BindHostReceiver(
    //     ukm_recorder.InitWithNewPipeAndPassReceiver());
    // deps.ukm_recorder =
    //     std::make_unique<ukm::MojoUkmRecorder>(std::move(ukm_recorder));
    return deps;
  }

  const gpu::GpuPreferences GetGpuPreferencesFromCommandLine() {
    DCHECK(base::CommandLine::InitializedForCurrentProcess());
    const base::CommandLine* command_line =
        base::CommandLine::ForCurrentProcess();
    gpu::GpuPreferences gpu_preferences =
        gpu::gles2::ParseGpuPreferences(command_line);
    gpu_preferences.disable_accelerated_video_decode = false;
    gpu_preferences.disable_accelerated_video_encode = false;
#if defined(OS_WIN)
    gpu_preferences.enable_low_latency_dxva = !false;
    gpu_preferences.enable_zero_copy_dxgi_video = !false;
    gpu_preferences.enable_nv12_dxgi_video = !false;
#endif
    gpu_preferences.disable_software_rasterizer = false;
    gpu_preferences.log_gpu_control_list_decisions = false;
    gpu_preferences.gpu_startup_dialog = false;
    gpu_preferences.disable_gpu_watchdog = false;
    gpu_preferences.gpu_sandbox_start_early = false;

    gpu_preferences.enable_oop_rasterization = false;
    gpu_preferences.disable_oop_rasterization = false;

    gpu_preferences.enable_oop_rasterization_ddl = false;
    gpu_preferences.enforce_vulkan_protected_memory = false;
    gpu_preferences.disable_vulkan_fallback_to_gl_for_testing = false;

#if defined(OS_MACOSX)
    gpu_preferences.enable_metal =
        base::FeatureList::IsEnabled(features::kMetal);
#endif

    gpu_preferences.enable_gpu_benchmarking_extension = false;

    gpu_preferences.enable_android_surface_control = false;

    // Some of these preferences are set or adjusted in
    // GpuDataManagerImplPrivate::AppendGpuCommandLine.
    return gpu_preferences;
  }

#pragma region viz::GpuHostImpl::Delegate
  // viz::GpuHostImpl::Delegate:
  gpu::GPUInfo GetGPUInfo() const override {
    DLOG(INFO) << __FUNCTION__;
    return gpu_info_;
  }
  gpu::GpuFeatureInfo GetGpuFeatureInfo() const override {
    DLOG(INFO) << __FUNCTION__;
    return gpu_feature_info_;
  }
  void DidInitialize(
      const gpu::GPUInfo& gpu_info,
      const gpu::GpuFeatureInfo& gpu_feature_info,
      const base::Optional<gpu::GPUInfo>& gpu_info_for_hardware_gpu,
      const base::Optional<gpu::GpuFeatureInfo>&
          gpu_feature_info_for_hardware_gpu,
      const gpu::GpuExtraInfo& gpu_extra_info) override {
    DLOG(INFO) << __FUNCTION__;
    gpu_info_ = gpu_info;
    gpu_feature_info_ = gpu_feature_info;
  }
  void DidFailInitialize() override { DLOG(INFO) << __FUNCTION__; }
  void DidCreateContextSuccessfully() override { DLOG(INFO) << __FUNCTION__; }
  // void MaybeShutdownGpuProcess() override { DLOG(INFO) << __FUNCTION__; }
#if defined(OS_WIN)
  void DidUpdateOverlayInfo(const gpu::OverlayInfo& overlay_info) override {
    DLOG(INFO) << __FUNCTION__;
  }
  void DidUpdateHDRStatus(bool hdr_enabled) override {
    DLOG(INFO) << __FUNCTION__;
  }
#endif
  void BlockDomainFrom3DAPIs(const GURL& url, gpu::DomainGuilt guilt) override {
    DLOG(INFO) << __FUNCTION__;
  }
  void DisableGpuCompositing() override { DLOG(INFO) << __FUNCTION__; }
  bool GpuAccessAllowed() const override {
    DLOG(INFO) << __FUNCTION__;
    return true;
  }
  gpu::ShaderCacheFactory* GetShaderCacheFactory() override {
    DLOG(INFO) << __FUNCTION__;
    return factory_instance_;
  }
  void RecordLogMessage(int32_t severity,
                        const std::string& header,
                        const std::string& message) override {
    DLOG(INFO) << __FUNCTION__;
  }
  void BindDiscardableMemoryReceiver(
      mojo::PendingReceiver<
          discardable_memory::mojom::DiscardableSharedMemoryManager> receiver)
      override {
    DLOG(INFO) << __FUNCTION__;
    discardable_memory::DiscardableSharedMemoryManager::Get()->Bind(
        std::move(receiver));
  }
  void BindInterface(const std::string& interface_name,
                     mojo::ScopedMessagePipeHandle interface_pipe) override {
    DLOG(INFO) << __FUNCTION__;
  }
  // void BindHostReceiver(mojo::GenericPendingReceiver generic_receiver)
  // override;
  void RunService(const std::string& service_name,
                  mojo::PendingReceiver<service_manager::mojom::Service>
                      receiver) override {
    DLOG(INFO) << __FUNCTION__;
  }
#if defined(USE_OZONE)
  void TerminateGpuProcess(const std::string& message) override;
#endif
#pragma endregion

#pragma region viz::VizMainImpl::Delegate
  // viz::VizMainImpl::Delegate:
  void OnInitializationFailed() override { DLOG(INFO) << __FUNCTION__; }
  void OnGpuServiceConnection(viz::GpuServiceImpl* gpu_service) override {
    DLOG(INFO) << __FUNCTION__;
  }
  void PostCompositorThreadCreated(
      base::SingleThreadTaskRunner* task_runner) override {
    DLOG(INFO) << __FUNCTION__;
  }
  void QuitMainMessageLoop() override { DLOG(INFO) << __FUNCTION__; }
#pragma endregion

#pragma region IPC::Listener implementation
  // IPC::Listener implementation:
  bool OnMessageReceived(const IPC::Message& msg) override {
    DLOG(INFO) << __FUNCTION__;
    return true;
  }
  void OnAssociatedInterfaceRequest(
      const std::string& interface_name,
      mojo::ScopedInterfaceEndpointHandle handle) override {
    DLOG(INFO) << __FUNCTION__;
    if (interface_name == viz::VizMainImpl::Name_) {
      if (gpu_main_thread_.task_runner()->BelongsToCurrentThread()) {
        viz_main_->BindAssociated(
            mojo::PendingAssociatedReceiver<viz::mojom::VizMain>(
                std::move(handle)));
      } else {
        gpu_main_thread_.task_runner()->PostTask(
            FROM_HERE, base::BindOnce(&GpuService::OnAssociatedInterfaceRequest,
                                      base::Unretained(this), interface_name,
                                      base::Passed(std::move(handle))));
      }
    }
  }
  void OnChannelConnected(int32_t peer_pid) override {
    DLOG(INFO) << __FUNCTION__;
  }
  void OnChannelError() override { DLOG(INFO) << __FUNCTION__; }
#pragma endregion

  base::Thread host_io_thread_;
  base::Thread gpu_io_thread_;
  base::Thread gpu_main_thread_;
  scoped_refptr<base::SingleThreadTaskRunner> host_main_thread_runner_;
  Compositor* compositor_;

  // Host
  gpu::ShaderCacheFactory* factory_instance_;
  std::unique_ptr<IPC::Channel> server_channel_;
  std::unique_ptr<viz::GpuHostImpl> gpu_host_;
  scoped_refptr<gpu::GpuChannelHost> gpu_channel_host_;
  int gpu_client_id_;
  std::unique_ptr<gpu::GpuMemoryBufferManager> gpu_memory_buffer_manager_;
  gpu::GPUInfo gpu_info_;
  gpu::GpuFeatureInfo gpu_feature_info_;

  // Gpu
  std::unique_ptr<IPC::SyncChannel> client_channel_;
  std::unique_ptr<viz::VizMainImpl> viz_main_;

  base::WaitableEvent shutdown_event_;
};

// DemoWindow creates the native window for the demo app. The native window
// provides a gfx::AcceleratedWidget, which is needed for the display
// compositor.
class DemoVizWindow : public ui::PlatformWindowDelegate {
 public:
  DemoVizWindow(base::OnceClosure close_closure)
      : close_closure_(std::move(close_closure)) {}
  ~DemoVizWindow() override = default;

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
    // We finally have a valid gfx::AcceleratedWidget. We can now start the
    // actual process of setting up the viz host and the service.
    // First, set up the mojo message-pipes that the host and the service will
    // use to communicate with each other.
    mojo::PendingRemote<viz::mojom::FrameSinkManager> frame_sink_manager;
    mojo::PendingReceiver<viz::mojom::FrameSinkManager>
        frame_sink_manager_receiver =
            frame_sink_manager.InitWithNewPipeAndPassReceiver();
    mojo::PendingRemote<viz::mojom::FrameSinkManagerClient>
        frame_sink_manager_client;
    mojo::PendingReceiver<viz::mojom::FrameSinkManagerClient>
        frame_sink_manager_client_receiver =
            frame_sink_manager_client.InitWithNewPipeAndPassReceiver();
    // Next, create the host and the service, and pass them the right ends of
    // the message-pipes.
    host_ = std::make_unique<Compositor>(
        widget_, platform_window_->GetBounds().size(),
        std::move(frame_sink_manager_client_receiver),
        std::move(frame_sink_manager));

    service_ = std::make_unique<GpuService>(
        std::move(frame_sink_manager_receiver),
        std::move(frame_sink_manager_client), host_.get());
  }

  // ui::PlatformWindowDelegate:
  void OnBoundsChanged(const gfx::Rect& new_bounds) override {
    host_->Resize(new_bounds.size());
  }

  void OnAcceleratedWidgetAvailable(gfx::AcceleratedWidget widget) override {
    widget_ = widget;
    if (platform_window_)
      InitializeDemo();
  }

  void OnDamageRect(const gfx::Rect& damaged_region) override {}
  void DispatchEvent(ui::Event* event) override {}
  void OnCloseRequest() override {
    // TODO: Use a more robust exit method
    platform_window_->Close();
    // service_.reset();
    // host_.reset();
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

  std::unique_ptr<Compositor> host_;
  std::unique_ptr<GpuService> service_;

  std::unique_ptr<ui::PlatformWindow> platform_window_;
  gfx::AcceleratedWidget widget_;
  base::OnceClosure close_closure_;

  DISALLOW_COPY_AND_ASSIGN(DemoVizWindow);
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
  demo::InitTrace("./trace_demo_viz_layer.json");
  demo::StartTrace(
      "viz,gpu,shell,ipc,mojom,skia,disabled-by-default-toplevel.flow");
  // 创建主消息循环，等价于 MessagLoop
  base::SingleThreadTaskExecutor main_task_executor(base::MessagePumpType::UI);
  // 初始化线程池，会创建新的线程，在新的线程中会创建新消息循环MessageLoop
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("DemoViews");

  // 初始化mojo
  mojo::core::Init();
  base::Thread mojo_thread("mojo");
  mojo_thread.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));
  auto ipc_support = std::make_unique<mojo::core::ScopedIPCSupport>(
      mojo_thread.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

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

  auto discardable_shared_memory_manager =
      std::make_unique<discardable_memory::DiscardableSharedMemoryManager>();

  base::RunLoop run_loop;

  auto use_gl = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      switches::kUseGL);
  g_use_gpu = use_gl != gl::kGLImplementationSwiftShaderForWebGLName &&
              use_gl != gl::kGLImplementationSwiftShaderName;

  demo::DemoVizWindow window(run_loop.QuitClosure());
  window.Create(gfx::Rect(800, 600));

  LOG(INFO) << "running...";
  run_loop.Run();

  {
    base::RunLoop run_loop;
    demo::FlushTrace(run_loop.QuitClosure());
    run_loop.Run();
  }

  return 0;
}
