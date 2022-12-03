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

#include "APILayer.h"

#include <loader_interfaces.h>

#include <memory>
#include <string>
#include <vector>

#include "DebugPrint.h"
#include "Environment.h"
#include "HandTrackingSource.h"
#include "OpenXRNext.h"
#include "PointCtrlSource.h"
#include "VirtualControllerSink.h"
#include "VirtualTouchScreenSink.h"
#include "openxr.h"

namespace HandTrackedCockpitClicking {

APILayer::APILayer(
  XrInstance instance,
  XrSession session,
  const std::shared_ptr<OpenXRNext>& next)
  : mOpenXR(next), mInstance(instance) {
  DebugPrint("{}()", __FUNCTION__);
  auto oxr = next.get();

  XrReferenceSpaceCreateInfo referenceSpace {
    .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
    .next = nullptr,
    .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW,
    .poseInReferenceSpace = XR_POSEF_IDENTITY,
  };

  if (!oxr->check_xrCreateReferenceSpace(
        session, &referenceSpace, &mViewSpace)) {
    DebugPrint("Failed to create view space");
    return;
  }
  referenceSpace.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  if (!oxr->check_xrCreateReferenceSpace(
        session, &referenceSpace, &mLocalSpace)) {
    DebugPrint("Failed to create world space");
    return;
  }

  if (Environment::Have_XR_EXT_HandTracking) {
    mHandTracking = std::make_unique<HandTrackingSource>(
      next, instance, session, mViewSpace, mLocalSpace);
  }
  mPointCtrl = std::make_unique<PointCtrlSource>(
    next, instance, session, mViewSpace, mLocalSpace);

  if (
    VirtualControllerSink::IsActionSink()
    || VirtualControllerSink::IsPointerSink()) {
    mVirtualController = std::make_unique<VirtualControllerSink>(
      next, instance, session, mViewSpace);
  }

  DebugPrint("Fully initialized.");
}

APILayer::~APILayer() {
  if (mViewSpace) {
    mOpenXR->xrDestroySpace(mViewSpace);
  }
  if (mLocalSpace) {
    mOpenXR->xrDestroySpace(mLocalSpace);
  }
}

XrResult APILayer::xrSuggestInteractionProfileBindings(
  XrInstance instance,
  const XrInteractionProfileSuggestedBinding* suggestedBindings) {
  if (mVirtualController) {
    return mVirtualController->xrSuggestInteractionProfileBindings(
      instance, suggestedBindings);
  }
  return mOpenXR->xrSuggestInteractionProfileBindings(
    instance, suggestedBindings);
}

XrResult APILayer::xrGetActionStateBoolean(
  XrSession session,
  const XrActionStateGetInfo* getInfo,
  XrActionStateBoolean* state) {
  if (mVirtualController) {
    return mVirtualController->xrGetActionStateBoolean(session, getInfo, state);
  }
  return mOpenXR->xrGetActionStateBoolean(session, getInfo, state);
}

XrResult APILayer::xrGetActionStateFloat(
  XrSession session,
  const XrActionStateGetInfo* getInfo,
  XrActionStateFloat* state) {
  if (mVirtualController) {
    return mVirtualController->xrGetActionStateFloat(session, getInfo, state);
  }
  return mOpenXR->xrGetActionStateFloat(session, getInfo, state);
}

XrResult APILayer::xrGetActionStatePose(
  XrSession session,
  const XrActionStateGetInfo* getInfo,
  XrActionStatePose* state) {
  if (mVirtualController) {
    return mVirtualController->xrGetActionStatePose(session, getInfo, state);
  }
  return mOpenXR->xrGetActionStatePose(session, getInfo, state);
}

XrResult APILayer::xrLocateSpace(
  XrSpace space,
  XrSpace baseSpace,
  XrTime time,
  XrSpaceLocation* location) {
  if (mVirtualController) {
    return mVirtualController->xrLocateSpace(space, baseSpace, time, location);
  }
  return mOpenXR->xrLocateSpace(space, baseSpace, time, location);
}

XrResult APILayer::xrSyncActions(
  XrSession session,
  const XrActionsSyncInfo* syncInfo) {
  if (mVirtualController) {
    return mVirtualController->xrSyncActions(session, syncInfo);
  }
  return mOpenXR->xrSyncActions(session, syncInfo);
}

XrResult APILayer::xrPollEvent(
  XrInstance instance,
  XrEventDataBuffer* eventData) {
  if (mVirtualController) {
    return mVirtualController->xrPollEvent(instance, eventData);
  }
  return mOpenXR->xrPollEvent(instance, eventData);
}

XrResult APILayer::xrGetCurrentInteractionProfile(
  XrSession session,
  XrPath topLevelUserPath,
  XrInteractionProfileState* interactionProfile) {
  if (mVirtualController) {
    return mVirtualController->xrGetCurrentInteractionProfile(
      session, topLevelUserPath, interactionProfile);
  }
  return mOpenXR->xrGetCurrentInteractionProfile(
    session, topLevelUserPath, interactionProfile);
}

XrResult APILayer::xrCreateActionSpace(
  XrSession session,
  const XrActionSpaceCreateInfo* createInfo,
  XrSpace* space) {
  if (mVirtualController) {
    return mVirtualController->xrCreateActionSpace(session, createInfo, space);
  }
  return mOpenXR->xrCreateActionSpace(session, createInfo, space);
}

XrResult APILayer::xrWaitFrame(
  XrSession session,
  const XrFrameWaitInfo* frameWaitInfo,
  XrFrameState* state) {
  const auto nextResult = mOpenXR->xrWaitFrame(session, frameWaitInfo, state);
  if (nextResult != XR_SUCCESS) {
    return nextResult;
  }

  FrameInfo frameInfo(
    mOpenXR.get(),
    mInstance,
    mLocalSpace,
    mViewSpace,
    state->predictedDisplayTime);

  if (
    (!mVirtualTouchScreen)
    && (VirtualTouchScreenSink::IsActionSink() || VirtualTouchScreenSink::IsPointerSink())) {
    mVirtualTouchScreen = std::make_unique<VirtualTouchScreenSink>(
      mOpenXR, session, state->predictedDisplayTime, mViewSpace);
  }

  InputState leftHand {XR_HAND_LEFT_EXT};
  InputState rightHand {XR_HAND_RIGHT_EXT};
  const auto pointerMode
    = (Config::PointerSink == PointerSink::VirtualTouchScreen)
    ? PointerMode::Direction
    : PointerMode::Pose;

  if (mHandTracking) {
    const auto [l, r] = mHandTracking->Update(
      (Config::PointerSource == PointerSource::OculusHandTracking)
        ? pointerMode
        : PointerMode::None,
      frameInfo);
    if (Config::PointerSource == PointerSource::OculusHandTracking) {
      leftHand.mPose = l.mPose;
      leftHand.mDirection = l.mDirection;
      rightHand.mPose = r.mPose;
      rightHand.mDirection = r.mDirection;
    }
    if (Config::PinchToClick) {
      leftHand.mPrimaryInteraction = l.mPrimaryInteraction;
      leftHand.mSecondaryInteraction = l.mSecondaryInteraction;
      rightHand.mPrimaryInteraction = r.mPrimaryInteraction;
      rightHand.mSecondaryInteraction = r.mSecondaryInteraction;
    }
    if (Config::PinchToScroll) {
      leftHand.mValueChange = l.mValueChange;
      rightHand.mValueChange = r.mValueChange;
    }
  }

  if (mPointCtrl) {
    const auto [l, r] = mPointCtrl->Update(pointerMode, frameInfo);
    if (Config::PointerSource == PointerSource::PointCtrl) {
      leftHand.mPose = l.mPose;
      leftHand.mDirection = l.mDirection;
      rightHand.mPose = r.mPose;
      rightHand.mDirection = r.mDirection;
    }
    if (Config::PointCtrlFCUClicks) {
      leftHand.mPrimaryInteraction
        = leftHand.mPrimaryInteraction || l.mPrimaryInteraction;
      leftHand.mSecondaryInteraction
        = leftHand.mSecondaryInteraction || l.mSecondaryInteraction;
      if (l.mValueChange != InputState::ValueChange::None) {
        leftHand.mValueChange = l.mValueChange;
      }
      rightHand.mPrimaryInteraction
        = rightHand.mPrimaryInteraction || r.mPrimaryInteraction;
      rightHand.mSecondaryInteraction
        = rightHand.mSecondaryInteraction || r.mSecondaryInteraction;
      if (r.mValueChange != InputState::ValueChange::None) {
        rightHand.mValueChange = r.mValueChange;
      }
    }
  }

  if (mVirtualTouchScreen) {
    mVirtualTouchScreen->Update(leftHand, rightHand);
  }

  if (mVirtualController) {
    mVirtualController->Update(frameInfo, leftHand, rightHand);
  }

  return XR_SUCCESS;
}

}// namespace HandTrackedCockpitClicking
