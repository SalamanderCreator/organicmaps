#include "testing/testing.hpp"

#include "drape_frontend/drape_frontend_tests/visual_params_fixture.hpp"
#include "drape_frontend/my_position_controller.hpp"

#include "geometry/screenbase.hpp"

namespace my_position_controller_tests
{
using df::test_support::VisualParamsFixture;

UNIT_CLASS_TEST(VisualParamsFixture, StopFollowModesAndFirstFix)
{
  ScreenBase screen;
  screen.OnSize(0, 0, 800, 600);
  screen.SetFromRect(m2::AnyRectD(m2::RectD(-100, -100, 100, 100)));
  location::GpsInfo fix;
  fix.m_latitude = 47;
  fix.m_longitude = 8;
  fix.m_horizontalAccuracy = 5;
  fix.m_timestamp = 1;

  for (auto mode : {location::Follow, location::FollowAndRotate, location::NotFollow})
  {
    df::MyPositionController controller(
        {mode, 0.0 /* timeInBackground */, {}, false /* isRoutingActive */, false /* isAutozoomEnabled */, {}},
        nullptr);
    controller.OnUpdateScreen(screen);
    controller.OnLocationUpdate(fix, false /* isNavigable */, screen);
    TEST_EQUAL(controller.GetCurrentMode(), mode, ());
    controller.StopLocationFollow();
    TEST_EQUAL(controller.GetCurrentMode(), location::NotFollow, ());
  }

  df::MyPositionController pending({location::Follow,
                                    0.0 /* timeInBackground */,
                                    {},
                                    false /* isRoutingActive */,
                                    false /* isAutozoomEnabled */,
                                    {}},
                                   nullptr);
  pending.OnUpdateScreen(screen);
  TEST_EQUAL(pending.GetCurrentMode(), location::PendingPosition, ());
  pending.StopLocationFollow();
  pending.OnLocationUpdate(fix, false /* isNavigable */, screen);
  TEST_EQUAL(pending.GetCurrentMode(), location::NotFollow, ());
}

UNIT_CLASS_TEST(VisualParamsFixture, CompassAndPositionButtons)
{
  ScreenBase screen;
  screen.OnSize(0, 0, 800, 600);
  screen.SetFromRect(m2::AnyRectD(m2::RectD(-100, -100, 100, 100)));
  location::GpsInfo fix;
  fix.m_latitude = 47;
  fix.m_longitude = 8;
  fix.m_horizontalAccuracy = 5;
  fix.m_timestamp = 1;

  df::MyPositionController controller({location::Follow,
                                       0.0 /* timeInBackground */,
                                       {},
                                       false /* isRoutingActive */,
                                       false /* isAutozoomEnabled */,
                                       {}},
                                      nullptr);
  controller.OnUpdateScreen(screen);
  controller.OnLocationUpdate(fix, false /* isNavigable */, screen);
  TEST_EQUAL(controller.GetCurrentMode(), location::Follow, ());

  // The compass button turns between north up and heading up.
  controller.ToggleNorthUp(screen);
  TEST_EQUAL(controller.GetCurrentMode(), location::FollowAndRotate, ());
  controller.ToggleNorthUp(screen);
  TEST_EQUAL(controller.GetCurrentMode(), location::Follow, ());

  // The position button returns to the orientation chosen last.
  controller.ToggleNorthUp(screen);
  controller.StopLocationFollow();
  controller.CenterOnPosition(screen);
  TEST_EQUAL(controller.GetCurrentMode(), location::FollowAndRotate, ());

  // The north lock keeps north up.
  controller.SetNorthLocked(true);
  TEST_EQUAL(controller.GetCurrentMode(), location::Follow, ());
  controller.ToggleNorthUp(screen);
  TEST_EQUAL(controller.GetCurrentMode(), location::Follow, ());
  controller.StopLocationFollow();
  controller.CenterOnPosition(screen);
  TEST_EQUAL(controller.GetCurrentMode(), location::Follow, ());
  controller.SetNorthLocked(false);

  // A map turned by hand goes back to north up first, without following.
  controller.StopLocationFollow();
  ScreenBase rotated = screen;
  rotated.SetAngle(1.0);
  controller.ToggleNorthUp(rotated);
  TEST_EQUAL(controller.GetCurrentMode(), location::NotFollow, ());
  controller.ToggleNorthUp(screen);
  TEST_EQUAL(controller.GetCurrentMode(), location::FollowAndRotate, ());
}
}  // namespace my_position_controller_tests
