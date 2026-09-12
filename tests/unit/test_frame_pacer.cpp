#include "test_framework.h"

#include "../../src/util/frame_pacer.h"

TEST(frame_pacer_renders_initial_frames_then_skips_idle) {
    frame_pacer::FramePacer pacer;
    int rendered = 0;
    int skipped = 0;
    for (int i = 0; i < 20; ++i) {
        auto decision = pacer.decide(false, false, false);
        if (decision.render) ++rendered;
        else ++skipped;
    }
    ASSERT_EQ(rendered, 4);
    ASSERT_EQ(skipped, 16);
}

TEST(frame_pacer_activity_resumes_rendering) {
    frame_pacer::FramePacer pacer;
    for (int i = 0; i < 20; ++i) pacer.decide(false, false, false);
    auto active = pacer.decide(false, true, false);
    ASSERT_TRUE(active.render);
    ASSERT_FALSE(active.sleep);
    ASSERT_TRUE(pacer.decide(false, false, false).render);
}

TEST(frame_pacer_disabled_always_renders) {
    frame_pacer::FramePacer pacer;
    pacer.enabled = false;
    for (int i = 0; i < 20; ++i)
        ASSERT_TRUE(pacer.decide(false, false, false).render);
    ASSERT_EQ(pacer.skipped, 0);
}

TEST(frame_pacer_pending_work_and_forced_capture_render_after_idle) {
    frame_pacer::FramePacer pacer;
    for (int i = 0; i < 20; ++i) pacer.decide(false, false, false);
    ASSERT_TRUE(pacer.decide(false, false, true).render);
    for (int i = 0; i < 20; ++i) pacer.decide(false, false, false);
    ASSERT_TRUE(pacer.decide(true, false, false).render);
}

TEST(frame_pacer_ready_unconsumed_tasks_do_not_hold_the_view_active) {
    std::promise<int> promise;
    auto task = promise.get_future();
    ASSERT_TRUE(frame_pacer::in_flight(task));
    promise.set_value(42);
    ASSERT_FALSE(frame_pacer::in_flight(task));
    frame_pacer::FramePacer pacer;
    for (int i = 0; i < 20; ++i) pacer.decide(false, false, frame_pacer::in_flight(task));
    ASSERT_EQ(pacer.skipped, 16);
    ASSERT_TRUE(task.valid());
    ASSERT_EQ(task.get(), 42);
    ASSERT_FALSE(frame_pacer::in_flight(task));
}

TEST(frame_pacer_draws_a_grace_frame_after_a_task_becomes_ready) {
    std::promise<int> promise;
    auto task = promise.get_future();
    frame_pacer::FramePacer pacer;
    for (int i = 0; i < 20; ++i) pacer.decide(false, false, false);
    ASSERT_TRUE(pacer.decide(false, false, frame_pacer::in_flight(task)).render);
    promise.set_value(42);
    ASSERT_TRUE(pacer.decide(false, false, frame_pacer::in_flight(task)).render);
}

int main() {
    printf("=== frame pacer tests ===\n");
    RUN_ALL_TESTS();
}
