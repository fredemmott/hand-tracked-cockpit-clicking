/*
 * MIT License
 *
 * Copyright (c) 2022 Fred Emmott <fred@fredemmott.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "HandTrackingSource.h"

#include <directxtk/SimpleMath.h>

#include <cmath>

#include "Config.h"
#include "Environment.h"

using namespace DirectX::SimpleMath;

namespace HandTrackedCockpitClicking {

HandTrackingSource::HandTrackingSource(
  const std::shared_ptr<OpenXRNext>& next,
  XrInstance instance,
  XrSession session,
  XrSpace viewSpace,
  XrSpace localSpace)
  : mOpenXR(next),
    mInstance(instance),
    mSession(session),
    mViewSpace(viewSpace),
    mLocalSpace(localSpace) {
  DebugPrint(
    "HandTrackingSource - PointerSource: {}; PinchToClick: {}; PinchToScroll: "
    "{}",
    Config::PointerSource == PointerSource::OpenXRHandTracking,
    Config::PinchToClick,
    Config::PinchToScroll);
}

HandTrackingSource::~HandTrackingSource() {
  for (const auto& hand: {mLeftHand, mRightHand}) {
    if (hand.mTracker) {
      mOpenXR->xrDestroyHandTrackerEXT(hand.mTracker);
    }
  }
}

template <class Actual, class Wanted>
static constexpr bool HasFlags(Actual actual, Wanted wanted) {
  return (actual & wanted) == wanted;
}

std::tuple<XrPosef, XrVector2f> HandTrackingSource::RaycastPose(
  const FrameInfo& frameInfo,
  const XrPosef& pose) {
  const auto& p = (pose * frameInfo.mLocalInView).position;
  const auto rx = std::atan2f(p.y, -p.z);
  const auto ry = std::atan2f(p.x, -p.z);

  const auto o = Quaternion::CreateFromAxisAngle(Vector3::UnitX, rx)
    * Quaternion::CreateFromAxisAngle(Vector3::UnitY, -ry);
  const XrPosef retView = {
    {o.x, o.y, o.z, o.w},
    pose.position,
  };

  return {
    {
      (retView * frameInfo.mViewInLocal).orientation,
      pose.position,
    },
    {rx, ry},
  };
}

static void PopulateInteractions(
  XrHandTrackingAimFlagsFB status,
  InputState* hand) {
  hand->mPrimaryInteraction = Config::PinchToClick
    && HasFlags(status, XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB);
  hand->mSecondaryInteraction = Config::PinchToClick
    && HasFlags(status, XR_HAND_TRACKING_AIM_MIDDLE_PINCHING_BIT_FB);
  if (!Config::PinchToScroll) {
    return;
  }

  if (HasFlags(status, XR_HAND_TRACKING_AIM_RING_PINCHING_BIT_FB)) {
    hand->mValueChange = InputState::ValueChange::Decrease;
    return;
  }
  if (HasFlags(status, XR_HAND_TRACKING_AIM_LITTLE_PINCHING_BIT_FB)) {
    hand->mValueChange = InputState::ValueChange::Increase;
    return;
  }
}

static bool UseHandTrackingAimPointFB() {
  return Config::UseHandTrackingAimPointFB
    && Environment::Have_XR_FB_HandTracking_Aim;
}

std::tuple<InputState, InputState> HandTrackingSource::Update(
  PointerMode,
  const FrameInfo& frameInfo) {
  this->UpdateHand(frameInfo, &mLeftHand);
  this->UpdateHand(frameInfo, &mRightHand);

  const auto& leftState = mLeftHand.mState;
  const auto& rightState = mRightHand.mState;
  if (!Config::OneHandOnly) {
    return {leftState, rightState};
  }

  if (!(leftState.mPose && rightState.mPose)) {
    return {leftState, rightState};
  }

  const auto leftActive = leftState.AnyInteraction();
  const auto rightActive = rightState.AnyInteraction();
  if (leftActive && !rightActive) {
    return {leftState, {XR_HAND_RIGHT_EXT}};
  }
  if (rightActive && !leftActive) {
    return {{XR_HAND_LEFT_EXT}, rightState};
  }

  const auto lrx = leftState.mDirection->x;
  const auto lry = leftState.mDirection->y;
  const auto ldiff = (lrx * lrx) + (lry * lry);

  const auto rrx = rightState.mDirection->x;
  const auto rry = rightState.mDirection->y;
  const auto rdiff = (rrx * rrx) + (rry * rry);
  if (ldiff < rdiff) {
    return {leftState, {XR_HAND_RIGHT_EXT}};
  }
  return {{XR_HAND_LEFT_EXT}, rightState};
}

void HandTrackingSource::KeepAlive(XrHandEXT handID, XrTime displayTime) {
  auto& hand = (handID == XR_HAND_LEFT_EXT) ? mLeftHand : mRightHand;
  hand.mLastKeepAliveAt = std::max(displayTime, hand.mLastKeepAliveAt);
}

void HandTrackingSource::UpdateHand(const FrameInfo& frameInfo, Hand* hand) {
  const auto displayTime = frameInfo.mPredictedDisplayTime;
  InitHandTracker(hand);
  auto& state = hand->mState;

  XrHandJointsLocateInfoEXT locateInfo {
    .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
    .baseSpace = mLocalSpace,
    .time = displayTime,
  };

  std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> jointLocations;
  std::array<XrHandJointVelocityEXT, XR_HAND_JOINT_COUNT_EXT> jointVelocities;
  jointLocations.fill({});
  jointVelocities.fill({});
  XrHandJointVelocitiesEXT velocities {
    .type = XR_TYPE_HAND_JOINT_VELOCITIES_EXT,
    .jointCount = jointVelocities.size(),
    .jointVelocities = jointVelocities.data(),
  };

  XrHandJointLocationsEXT joints {
    .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
    .next = &velocities,
    .jointCount = jointLocations.size(),
    .jointLocations = jointLocations.data(),
  };

  XrHandTrackingAimStateFB aimFB {XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
  if (Environment::Have_XR_FB_HandTracking_Aim) {
    velocities.next = &aimFB;
  }

  if (!mOpenXR->check_xrLocateHandJointsEXT(
        hand->mTracker, &locateInfo, &joints)) {
    state = {hand->mHand};
    return;
  }

  if (UseHandTrackingAimPointFB()) {
    if (HasFlags(aimFB.status, XR_HAND_TRACKING_AIM_VALID_BIT_FB)) {
      state = {
        .mHand = hand->mHand,
        .mUpdatedAt = displayTime,
        .mPose = {aimFB.aimPose},
      };
    }
  } else if (joints.isActive) {
    const auto joint = jointLocations[Config::HandTrackingAimJoint];
    if (
      HasFlags(joint.locationFlags, XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
      && HasFlags(joint.locationFlags, XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
      state = {
        .mHand = hand->mHand,
        .mUpdatedAt = displayTime,
        .mPose = {joint.pose},
      };
    }
  }

  const auto velocity3 = jointVelocities[Config::HandTrackingAimJoint];
  if (HasFlags(velocity3.velocityFlags, XR_SPACE_VELOCITY_LINEAR_VALID_BIT)) {
    const auto linearVelocity = std::sqrt(
      (velocity3.linearVelocity.x * velocity3.linearVelocity.x)
      + (velocity3.linearVelocity.y * velocity3.linearVelocity.y)
      + (velocity3.linearVelocity.z * velocity3.linearVelocity.z));
    if (linearVelocity >= Config::HandTrackingSleepSpeed) {
      hand->mLastKeepAliveAt = displayTime;
      if (
        hand->mSleeping && linearVelocity >= Config::HandTrackingWakeSpeed
        && std::chrono::nanoseconds(displayTime - hand->mLastSleepSpeedAt)
          > std::chrono::milliseconds(Config::HandTrackingWakeMilliseconds)) {
        DebugPrint(
          "{} > {}, waking hand {}",
          linearVelocity,
          Config::HandTrackingWakeSpeed,
          static_cast<int>(hand->mHand));
        hand->mSleeping = false;
      }
    } else {
      hand->mLastSleepSpeedAt = displayTime;
      if (
        (!hand->mSleeping)
        && std::chrono::nanoseconds(displayTime - hand->mLastKeepAliveAt)
          > std::chrono::milliseconds(Config::HandTrackingSleepMilliseconds)) {
        DebugPrint(
          "{} < {} for at least {}ms, sleeping hand {}",
          linearVelocity,
          Config::HandTrackingSleepSpeed,
          Config::HandTrackingSleepMilliseconds,
          static_cast<int>(hand->mHand));
        hand->mSleeping = true;
      }
    }
  }

  if (hand->mSleeping) {
    state = {hand->mHand};
    return;
  }

  const auto age = std::chrono::nanoseconds(frameInfo.mNow - state.mUpdatedAt);
  const auto stale = age > std::chrono::milliseconds(200);

  if (stale) {
    state = {hand->mHand};
    return;
  }

  PopulateInteractions(aimFB.status, &state);
  const auto [raycastPose, direction] = RaycastPose(frameInfo, *state.mPose);
  state.mDirection = {direction};
  if (Config::HandTrackingOrientation == HandTrackingOrientation::RayCast) {
    state.mPose = raycastPose;
  }
}

void HandTrackingSource::InitHandTracker(Hand* hand) {
  if (hand->mTracker) [[likely]] {
    return;
  }

  XrHandTrackerCreateInfoEXT createInfo {
    .type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
    .hand = hand->mHand,
    .handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT,
  };
  if (!mOpenXR->check_xrCreateHandTrackerEXT(
        mSession, &createInfo, &hand->mTracker)) {
    DebugPrint(
      "Failed to initialize hand tracker for hand {}",
      static_cast<int>(hand->mHand));
    return;
  }

  DebugPrint("Initialized hand tracker {}.", static_cast<int>(hand->mHand));
}

}// namespace HandTrackedCockpitClicking
