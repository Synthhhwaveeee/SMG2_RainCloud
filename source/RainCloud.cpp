#include "RainCloud.h"
#include "syati.h"

namespace {
    const f32 sNoShadowDropLengthMax = 2000.0f;
    const f32 sRainCylinderBaseLength = 1000.0f;
    const f32 sSwitchEffectBorder = 1500.0f;
    static s32 sThunderStep = 140;
};

namespace NrvRainCloud {
    FULL_NERVE(RainCloudNrvAppear, RainCloud, Appear);
    FULL_NERVE(RainCloudNrvWait, RainCloud, Wait);
    FULL_NERVE(RainCloudNrvDisappear, RainCloud, Disappear);
    FULL_NERVE(RainCloudNrvEnd, RainCloud, End);
    FULL_NERVE(RainCloudNrvSoftTouch, RainCloud, SoftTouch);
    FULL_NERVE(RainCloudNrvHardTouch, RainCloud, HardTouch);
};

RainCloud::RainCloud(const char* pName)
    : LiveActor(pName), mCloudCylinder(), mRailMover(), mLodCtrl(), mClippingCenter(0.0f, 0.0f, 0.0f), mRainLength(), mShadowDropLength(-1.0f),
      mIsOnPlayer(), mNoRain() {
    mRainCylinderMtx.identity();
    mSplashMtx.identity();
}

#pragma push
#pragma global_optimizer off
void RainCloud::init(const JMapInfoIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("RainCloud", nullptr, nullptr, false);
    MR::getJMapInfoArg3NoInit(rIter, &mNoRain);

    if (!mNoRain) {
        mRainCylinderMtx.set(getBaseMtx());
        mCloudCylinder = new ModelObj("雨雲雨範囲", "RainCloudCylinder", mRainCylinderMtx, -2, -2, -2, false);
        mCloudCylinder->initWithoutIter();
        MR::invalidateClipping(mCloudCylinder);
    }

    MR::connectToSceneCollisionMapObj(this);

    if (!mNoRain) {
        initHitSensor(2);
        MR::addBodyMessageSensorMapObj(this);
        MR::addHitSensorCallback(this, "drop_water", ATYPE_MAP_OBJ_SIMPLE, 4, 150.0f);
    } else {
        initHitSensor(1);
        MR::addBodyMessageSensorMapObj(this);
    }

    MR::initCollisionParts(this, "RainCloud", getSensor("Body"), nullptr);
    initEffectKeeper(0, nullptr, false);

    if (!mNoRain) {
        MR::setEffectHostMtx(this, "Splash", mSplashMtx);
    }

    initSound(4, "RainCloud", false, TVec3f(0.0f, 0.0f, 0.0f));

    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        mRailMover = new MapPartsRailMover(this);
        mRailMover->init(rIter);
        mRailMover->start();
    }

    mLodCtrl = MR::createLodCtrlPlanet(this, rIter, -1.0f, -1);
    MR::initShadowVolumeFlatModel(this, "RainCloudVolume", MR::getJointMtx(this, "Shadow"));
    MR::getJMapInfoArg0NoInit(rIter, &mShadowDropLength);

    f32 dropLength;
    if ((dropLength = mShadowDropLength) != -1.0f) {
        mRainLength = dropLength;
        MR::setShadowDropLength(this, nullptr, dropLength);
        MR::setShadowVolumeEndDropOffset(this, nullptr, 80.0f);
        MR::excludeCalcShadowToMyCollision(this, nullptr);
        MR::onShadowVolumeCutDropLength(this, nullptr);
        MR::onCalcShadow(this, nullptr);
    } else {
        mShadowDropLength = dropLength = ::sNoShadowDropLengthMax;
        MR::invalidateShadow(this, nullptr);
    }

    TVec3f clippingCenter;
    clippingCenter = mTranslation;
    f32 boundRadius;
    MR::calcModelBoundingRadius(&boundRadius, this);
    boundRadius += 0.5f * mRainLength;

    if (mRailMover != nullptr) {
        f32 railRadius;
        MR::calcRailClippingInfo(&clippingCenter, &railRadius, this, 100.0f, 500.0f);
        boundRadius += railRadius;
    }

    JMAVECScaleAdd(mGravity, mTranslation, mClippingCenter, 0.5f * mRainLength);
    MR::setClippingTypeSphere(this, boundRadius, &mClippingCenter);
    MR::setGroupClipping(this, rIter, 16);
    MR::joinToGroupArray(this, rIter, "雲集団", 16);
    MR::startBck(this, "Wait", nullptr);

    if (!mNoRain) {
        MR::startBpk(this, "Rain");
        MR::startBpk(mLodCtrl->mModelObjLow, "Rain");
    } else {
        MR::startBpk(this, "Fine");
        MR::startBpk(mLodCtrl->mModelObjLow, "Fine");
    }

    initNerve(&NrvRainCloud::RainCloudNrvWait::sInstance, 0);
    makeActorAppeared();
}
#pragma pop

void RainCloud::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Appear", nullptr);
        MR::startBpk(this, "Appear");
        MR::validateCollisionParts(this);
        mCloudCylinder->appear();
        MR::startSound(this, "SE_OJ_RAIN_CLOUD_APPEAR");
    }

    if (MR::isStep(this, ::sThunderStep)) {
        MR::startSound(this, "SE_OJ_RAIN_CLOUD_THUNDER");
    }

    if (MR::isBckStopped(this)) {
        MR::validateHitSensors(this);
        setNerve(&NrvRainCloud::RainCloudNrvWait::sInstance);
    }
}

void RainCloud::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait", nullptr);
    }

    if (!MR::isOnPlayer(this) && mIsOnPlayer) {
        mIsOnPlayer = false;
        setNerve(&NrvRainCloud::RainCloudNrvSoftTouch::sInstance);
    }
}

void RainCloud::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Disappear", nullptr);
        MR::invalidateHitSensors(this);
        MR::invalidateCollisionParts(this);
        MR::startSound(this, "SE_OJ_RAIN_CLOUD_DISAPPEAR");
    }

    if (MR::isBckStopped(this)) {
        mCloudCylinder->kill();
        mIsOnPlayer = false;
    }

    s32 railPointNo = MR::getCurrentRailPointNo(this);

    if (mRailMover->mMoveStopType == 2 && railPointNo <= 0 && railPointNo != mRailMover->mPointPassChecker->mCurrentRailPointNo) {
        setNerve(&NrvRainCloud::RainCloudNrvEnd::sInstance);
    }
}

void RainCloud::exeEnd() {
    if (MR::isBckStopped(this) && isNextStartOK()) {
        if (mCloudCylinder != nullptr) {
            mCloudCylinder->kill();
        }

        setNerve(&NrvRainCloud::RainCloudNrvAppear::sInstance);
    }
}

void RainCloud::exeSoftTouch() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "OnBee", nullptr);
    }

    if (MR::isBckStopped(this)) {
        mIsOnPlayer = MR::isOnPlayer(this);
        setNerve(&NrvRainCloud::RainCloudNrvWait::sInstance);
    }
}

void RainCloud::exeHardTouch() {
    if (MR::isFirstStep(this)) {
        MR::invalidateCollisionParts(this);
        MR::tryRumblePadMiddle(this, 0);
        MR::shakeCameraNormal();
    }

    if (MR::isStep(this, 60)) {
        MR::validateCollisionParts(this);
        setNerve(&NrvRainCloud::RainCloudNrvWait::sInstance);
    }
}

void RainCloud::control() {
    mLodCtrl->update();

    if (mRailMover != nullptr && !isNerve(&NrvRainCloud::RainCloudNrvEnd::sInstance)) {
        mRailMover->movement();
        mTranslation.set(mRailMover->mTranslation);
        s32 railPointNo = MR::getCurrentRailPointNo(this);

        if (mRailMover->mMoveStopType == 2 && railPointNo == MR::getRailPointNum(this) - 1 && railPointNo != mRailMover->mPointPassChecker->mCurrentRailPointNo) {
            setNerve(&NrvRainCloud::RainCloudNrvDisappear::sInstance);
            return;
        }
    }

    if (MR::isShadowProjected(this, nullptr)) {
        mRainLength = MR::getShadowProjectionLength(this, nullptr);
    } else {
        mRainLength = mShadowDropLength;
    }

    if (!mNoRain) {
        switchEffect();
        updateRainCylinder();
    }
}

void RainCloud::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::isPlayerElementModeBee()) {
            MR::curePlayerElementMode();
            MR::tryRumblePadStrong(this, 0);
        }

        MR::sendArbitraryMsg(ACTMES_PUDDLE_TOUCH_GROUND, pReceiver, pSender);
    }
}

bool RainCloud::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_END_DEMO && !mNoRain) {
        switchEffect();
        return true;
    }

    bool isChanging = isNerve(&NrvRainCloud::RainCloudNrvAppear::sInstance) || isNerve(&NrvRainCloud::RainCloudNrvDisappear::sInstance) ||
                      isNerve(&NrvRainCloud::RainCloudNrvEnd::sInstance);
    if (isChanging) {
        return false;
    }

    if (isNerve(&NrvRainCloud::RainCloudNrvHardTouch::sInstance)) {
        return false;
    }

    if (isNerve(&NrvRainCloud::RainCloudNrvSoftTouch::sInstance)) {
        return false;
    }

    if (MR::isMsgFloorTouch(msg)) {
        if (!MR::isPlayerElementModeBee()) {
            setNerve(&NrvRainCloud::RainCloudNrvHardTouch::sInstance);
        } else if (!mIsOnPlayer) {
            setNerve(&NrvRainCloud::RainCloudNrvSoftTouch::sInstance);
        }

        return true;
    }

    return false;
}

void RainCloud::updateHitSensor(HitSensor* pSensor) {
    f32 radius = pSensor->mRadius;
    TVec3f start;
    JMAVECScaleAdd(mGravity, mTranslation, start, radius);
    TVec3f end;
    JMAVECScaleAdd(mGravity, mTranslation, end, mRainLength - radius);

    MR::calcPerpendicFootToLineInside(&pSensor->mPosition, *MR::getPlayerPos(), start, end);
}

void RainCloud::startClipped() {
    MR::deleteEffectAll(this);
    LiveActor::startClipped();

    if (mCloudCylinder != nullptr) {
        mCloudCylinder->startClipped();
    }
}

void RainCloud::endClipped() {
    LiveActor::endClipped();

    if (mCloudCylinder != nullptr) {
        mCloudCylinder->endClipped();
    }

    if (!mNoRain && !MR::isDemoActive()) {
        switchEffect();
    }
}

void RainCloud::switchEffect() {
    bool isChanging = isNerve(&NrvRainCloud::RainCloudNrvAppear::sInstance) || isNerve(&NrvRainCloud::RainCloudNrvDisappear::sInstance) ||
                      isNerve(&NrvRainCloud::RainCloudNrvEnd::sInstance);

    if (isChanging) {
        if (MR::isEffectValid(this, "Splash")) {
            MR::deleteEffect(this, "Splash");
        }

        if (MR::isEffectValid(this, "Line20")) {
            MR::deleteEffect(this, "Line20");
        }

        if (MR::isEffectValid(this, "Line10")) {
            MR::deleteEffect(this, "Line10");
        }
    } else {
        bool isLongRain;
        if (MR::isShadowProjected(this, nullptr)) {
            isLongRain = mRainLength > ::sSwitchEffectBorder;
            TVec3f projectionPos;
            MR::getShadowProjectionPos(this, nullptr, &projectionPos);
            TVec3f projectionNormal;
            MR::getShadowProjectionNormal(this, nullptr, &projectionNormal);
            MR::makeMtxUpNoSupportPos(&mSplashMtx, projectionNormal, projectionPos);

            if (!MR::isEffectValid(this, "Splash")) {
                MR::emitEffect(this, "Splash");
            }
        } else {
            isLongRain = true;
            MR::deleteEffect(this, "Splash");
        }

        if (isLongRain && !MR::isEffectValid(this, "Line20")) {
            MR::deleteEffect(this, "Line10");
            MR::emitEffect(this, "Line20");
        } else if (!isLongRain && !MR::isEffectValid(this, "Line10")) {
            MR::deleteEffect(this, "Line20");
            MR::emitEffect(this, "Line10");
        }
    }
}

void RainCloud::updateRainCylinder() {
    if (MR::isDead(mCloudCylinder)) {
        return;
    }

    mRainCylinderMtx.set(MR::getJointMtx(this, "Shadow"));
    f32 lengthScale = mRainLength / ::sRainCylinderBaseLength;
    f32 widthScale = 1.0f;
    if (isNerve(&NrvRainCloud::RainCloudNrvAppear::sInstance) || isNerve(&NrvRainCloud::RainCloudNrvDisappear::sInstance)) {
        if (MR::isBckPlaying(this, "Appear")) {
            widthScale = MR::getBckFrame(this) / MR::getBckFrameMax(this);
        } else if (MR::isBckPlaying(this, "Disappear")) {
            widthScale = 1.0f - MR::getBckFrame(this) / MR::getBckFrameMax(this);
        }

        widthScale = MR::clamp(widthScale, 0.1f, 1.0f);
    }

    MR::preScaleMtx(mRainCylinderMtx, TVec3f(widthScale, lengthScale, widthScale));
    MR::startLevelSound(this, "SE_OJ_LV_FOREST_RAIN");
}

bool RainCloud::isNextStartOK() const {
    if (MR::getGroupFromArray(this) == nullptr) {
        return true;
    }

    LiveActor* pCloud;
    LiveActorGroup* group = MR::getGroupFromArray(this);
    for (s32 i = 0; i < group->mNumObjs; i++) {
        pCloud = group->getActor(i);
        bool isReady = pCloud->isNerve(&NrvRainCloud::RainCloudNrvEnd::sInstance) ||
                       (pCloud->isNerve(&NrvRainCloud::RainCloudNrvAppear::sInstance) && MR::isFirstStep(pCloud));
        if (!isReady) {
            return false;
        }
    }

    return true;
}

RainCloud::~RainCloud() {
}
