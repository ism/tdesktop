/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/timer.h"
#include "base/bytes.h"
#include "mtproto/sender.h"
#include "mtproto/mtproto_auth_key.h"

class History;

namespace tgcalls {
class GroupInstanceCustomImpl;
struct GroupLevelsUpdate;
struct GroupNetworkState;
struct GroupParticipantDescription;
class VideoCaptureInterface;
} // namespace tgcalls

namespace base {
class GlobalShortcutManager;
class GlobalShortcutValue;
} // namespace base

namespace Webrtc {
class MediaDevices;
class VideoTrack;
} // namespace Webrtc

namespace Data {
struct LastSpokeTimes;
struct GroupCallParticipant;
class GroupCall;
} // namespace Data

namespace Calls {

namespace Group {
struct MuteRequest;
struct VolumeRequest;
struct ParticipantState;
struct JoinInfo;
struct RejoinEvent;
} // namespace Group

enum class MuteState {
	Active,
	PushToTalk,
	Muted,
	ForceMuted,
	RaisedHand,
};

[[nodiscard]] inline auto MapPushToTalkToActive() {
	return rpl::map([=](MuteState state) {
		return (state == MuteState::PushToTalk) ? MuteState::Active : state;
	});
}

[[nodiscard]] bool IsGroupCallAdmin(
	not_null<PeerData*> peer,
	not_null<PeerData*> participantPeer);

struct LevelUpdate {
	uint32 ssrc = 0;
	float value = 0.;
	bool voice = false;
	bool me = false;
};

struct StreamsVideoUpdate {
	uint32 ssrc = 0;
	bool streams = false;
};

struct VideoParams;

[[nodiscard]] std::shared_ptr<VideoParams> ParseVideoParams(
	const QByteArray &video,
	const QByteArray &screencast,
	const std::shared_ptr<VideoParams> &existing);
[[nodiscard]] const base::flat_set<uint32> &VideoSourcesFromParams(
	const std::shared_ptr<VideoParams> &params);

class GroupCall final : public base::has_weak_ptr {
public:
	class Delegate {
	public:
		virtual ~Delegate() = default;

		virtual void groupCallFinished(not_null<GroupCall*> call) = 0;
		virtual void groupCallFailed(not_null<GroupCall*> call) = 0;
		virtual void groupCallRequestPermissionsOrFail(
			Fn<void()> onSuccess) = 0;

		enum class GroupCallSound {
			Started,
			Connecting,
			AllowedToSpeak,
			Ended,
		};
		virtual void groupCallPlaySound(GroupCallSound sound) = 0;
		virtual auto groupCallGetVideoCapture(const QString &deviceId)
			-> std::shared_ptr<tgcalls::VideoCaptureInterface> = 0;
	};

	using GlobalShortcutManager = base::GlobalShortcutManager;

	GroupCall(
		not_null<Delegate*> delegate,
		Group::JoinInfo info,
		const MTPInputGroupCall &inputCall);
	~GroupCall();

	[[nodiscard]] uint64 id() const {
		return _id;
	}
	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}
	[[nodiscard]] not_null<PeerData*> joinAs() const {
		return _joinAs;
	}
	[[nodiscard]] bool showChooseJoinAs() const;
	[[nodiscard]] TimeId scheduleDate() const {
		return _scheduleDate;
	}
	[[nodiscard]] bool scheduleStartSubscribed() const;

	[[nodiscard]] Data::GroupCall *lookupReal() const;
	[[nodiscard]] rpl::producer<not_null<Data::GroupCall*>> real() const;

	void start(TimeId scheduleDate);
	void hangup();
	void discard();
	void rejoinAs(Group::JoinInfo info);
	void rejoinWithHash(const QString &hash);
	void join(const MTPInputGroupCall &inputCall);
	void handleUpdate(const MTPUpdate &update);
	void handlePossibleCreateOrJoinResponse(const MTPDupdateGroupCall &data);
	void handlePossibleCreateOrJoinResponse(
		const MTPDupdateGroupCallConnection &data);
	void changeTitle(const QString &title);
	void toggleRecording(bool enabled, const QString &title);
	[[nodiscard]] bool recordingStoppedByMe() const {
		return _recordingStoppedByMe;
	}
	void startScheduledNow();
	void toggleScheduleStartSubscribed(bool subscribed);

	void addVideoOutput(uint32 ssrc, not_null<Webrtc::VideoTrack*> track);
	[[nodiscard]] not_null<Webrtc::VideoTrack*> outgoingVideoTrack() const;

	void setMuted(MuteState mute);
	void setMutedAndUpdate(MuteState mute);
	[[nodiscard]] MuteState muted() const {
		return _muted.current();
	}
	[[nodiscard]] rpl::producer<MuteState> mutedValue() const {
		return _muted.value();
	}

	[[nodiscard]] bool videoCall() const {
		return _videoCall.current();
	}
	[[nodiscard]] rpl::producer<bool> videoCallValue() const {
		return _videoCall.value();
	}

	[[nodiscard]] auto otherParticipantStateValue() const
		-> rpl::producer<Group::ParticipantState>;

	enum State {
		Creating,
		Waiting,
		Joining,
		Connecting,
		Joined,
		FailedHangingUp,
		Failed,
		HangingUp,
		Ended,
	};
	[[nodiscard]] State state() const {
		return _state.current();
	}
	[[nodiscard]] rpl::producer<State> stateValue() const {
		return _state.value();
	}

	enum class InstanceState {
		Disconnected,
		TransitionToRtc,
		Connected,
	};
	[[nodiscard]] InstanceState instanceState() const {
		return _instanceState.current();
	}
	[[nodiscard]] rpl::producer<InstanceState> instanceStateValue() const {
		return _instanceState.value();
	}

	[[nodiscard]] rpl::producer<LevelUpdate> levelUpdates() const {
		return _levelUpdates.events();
	}
	[[nodiscard]] auto streamsVideoUpdates() const
	-> rpl::producer<StreamsVideoUpdate> {
		return _streamsVideoUpdated.events();
	}
	[[nodiscard]] bool streamsVideo(uint32 ssrc) const {
		return ssrc
			&& _videoStreamSsrcs.contains(ssrc)
			&& !_videoMuted.contains(ssrc);
	}
	[[nodiscard]] uint32 videoStreamPinned() const {
		return _videoStreamPinned;
	}
	void pinVideoStream(uint32 ssrc);
	[[nodiscard]] uint32 videoStreamLarge() const {
		return _videoStreamLarge.current();
	}
	[[nodiscard]] rpl::producer<uint32> videoStreamLargeValue() const {
		return _videoStreamLarge.value();
	}
	[[nodiscard]] Webrtc::VideoTrack *videoLargeTrack() const {
		return _videoLargeTrack.current();
	}
	[[nodiscard]] auto videoLargeTrackValue() const
	-> rpl::producer<Webrtc::VideoTrack*> {
		return _videoLargeTrack.value();
	}
	[[nodiscard]] rpl::producer<Group::RejoinEvent> rejoinEvents() const {
		return _rejoinEvents.events();
	}
	[[nodiscard]] rpl::producer<> allowedToSpeakNotifications() const {
		return _allowedToSpeakNotifications.events();
	}
	[[nodiscard]] rpl::producer<> titleChanged() const {
		return _titleChanged.events();
	}
	static constexpr auto kSpeakLevelThreshold = 0.2;

	void setCurrentAudioDevice(bool input, const QString &deviceId);
	void setCurrentVideoDevice(const QString &deviceId);
	[[nodiscard]] bool isScreenSharing() const;
	[[nodiscard]] QString screenSharingDeviceId() const;
	void toggleVideo(bool active);
	void switchToScreenSharing(const QString &uniqueId);

	void toggleMute(const Group::MuteRequest &data);
	void changeVolume(const Group::VolumeRequest &data);
	std::variant<int, not_null<UserData*>> inviteUsers(
		const std::vector<not_null<UserData*>> &users);

	std::shared_ptr<GlobalShortcutManager> ensureGlobalShortcutManager();
	void applyGlobalShortcutChanges();

	void pushToTalk(bool pressed, crl::time delay);

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	class LoadPartTask;

public:
	void broadcastPartStart(std::shared_ptr<LoadPartTask> task);
	void broadcastPartCancel(not_null<LoadPartTask*> task);

private:
	using GlobalShortcutValue = base::GlobalShortcutValue;
	struct LargeTrack;

	struct LoadingPart {
		std::shared_ptr<LoadPartTask> task;
		mtpRequestId requestId = 0;
	};

	enum class FinishType {
		None,
		Ended,
		Failed,
	};
	enum class InstanceMode {
		None,
		Rtc,
		Stream,
	};
	enum class SendUpdateType {
		Mute,
		RaiseHand,
		VideoMuted,
	};

	void handlePossibleCreateOrJoinResponse(const MTPDgroupCall &data);
	void handlePossibleDiscarded(const MTPDgroupCallDiscarded &data);
	void handleUpdate(const MTPDupdateGroupCall &data);
	void handleUpdate(const MTPDupdateGroupCallParticipants &data);
	void ensureControllerCreated();
	void destroyController();

	void setState(State state);
	void finish(FinishType type);
	void maybeSendMutedUpdate(MuteState previous);
	void sendSelfUpdate(SendUpdateType type);
	void updateInstanceMuteState();
	void updateInstanceVolumes();
	void applyMeInCallLocally();
	void rejoin();
	void rejoin(not_null<PeerData*> as);
	void setJoinAs(not_null<PeerData*> as);
	void saveDefaultJoinAs(not_null<PeerData*> as);
	void subscribeToReal(not_null<Data::GroupCall*> real);
	void setScheduledDate(TimeId date);

	void audioLevelsUpdated(const tgcalls::GroupLevelsUpdate &data);
	void setInstanceConnected(tgcalls::GroupNetworkState networkState);
	void setInstanceMode(InstanceMode mode);
	void checkLastSpoke();
	void pushToTalkCancel();

	void checkGlobalShortcutAvailability();
	void checkJoined();
	void checkFirstTimeJoined();
	void notifyAboutAllowedToSpeak();

	void playConnectingSound();
	void stopConnectingSound();
	void playConnectingSoundOnce();

	void requestParticipantsInformation(const std::vector<uint32_t> &ssrcs);
	void addParticipantsToInstance();
	void prepareParticipantForAdding(
		const Data::GroupCallParticipant &participant);
	void addPreparedParticipants();
	void addPreparedParticipantsDelayed();
	void setVideoStreams(const std::vector<std::uint32_t> &ssrcs);
	[[nodiscard]] uint32 chooseLargeVideoSsrc() const;

	void editParticipant(
		not_null<PeerData*> participantPeer,
		bool mute,
		std::optional<int> volume);
	void applyParticipantLocally(
		not_null<PeerData*> participantPeer,
		bool mute,
		std::optional<int> volume);
	void applyQueuedSelfUpdates();
	void applySelfUpdate(const MTPDgroupCallParticipant &data);
	void applyOtherParticipantUpdate(const MTPDgroupCallParticipant &data);

	void setupMediaDevices();
	void setupOutgoingVideo();

	[[nodiscard]] MTPInputGroupCall inputCall() const;

	const not_null<Delegate*> _delegate;
	not_null<PeerData*> _peer; // Can change in legacy group migration.
	rpl::event_stream<PeerData*> _peerStream;
	not_null<History*> _history; // Can change in legacy group migration.
	MTP::Sender _api;
	rpl::event_stream<not_null<Data::GroupCall*>> _realChanges;
	rpl::variable<State> _state = State::Creating;
	rpl::variable<InstanceState> _instanceState
		= InstanceState::Disconnected;
	bool _instanceTransitioning = false;
	InstanceMode _instanceMode = InstanceMode::None;
	base::flat_set<uint32> _unresolvedSsrcs;
	std::vector<tgcalls::GroupParticipantDescription> _preparedParticipants;
	bool _addPreparedParticipantsScheduled = false;
	bool _recordingStoppedByMe = false;

	MTP::DcId _broadcastDcId = 0;
	base::flat_map<not_null<LoadPartTask*>, LoadingPart> _broadcastParts;

	not_null<PeerData*> _joinAs;
	std::vector<not_null<PeerData*>> _possibleJoinAs;
	QString _joinHash;

	rpl::variable<MuteState> _muted = MuteState::Muted;
	rpl::variable<bool> _videoCall = false;
	bool _initialMuteStateSent = false;
	bool _acceptFields = false;

	rpl::event_stream<Group::ParticipantState> _otherParticipantStateValue;
	std::vector<MTPGroupCallParticipant> _queuedSelfUpdates;

	uint64 _id = 0;
	uint64 _accessHash = 0;
	uint32 _mySsrc = 0;
	uint32 _screencastSsrc = 0;
	TimeId _scheduleDate = 0;
	base::flat_set<uint32> _mySsrcs;
	mtpRequestId _createRequestId = 0;
	mtpRequestId _updateMuteRequestId = 0;

	std::unique_ptr<tgcalls::GroupInstanceCustomImpl> _instance;
	std::shared_ptr<tgcalls::VideoCaptureInterface> _videoCapture;
	const std::unique_ptr<Webrtc::VideoTrack> _videoOutgoing;
	rpl::event_stream<LevelUpdate> _levelUpdates;
	rpl::event_stream<StreamsVideoUpdate> _streamsVideoUpdated;
	base::flat_set<uint32> _videoStreamSsrcs;
	base::flat_set<uint32> _videoMuted;
	rpl::variable<uint32> _videoStreamLarge = 0;
	uint32 _videoStreamPinned = 0;
	std::unique_ptr<LargeTrack> _videoLargeTrackWrap;
	rpl::variable<Webrtc::VideoTrack*> _videoLargeTrack;
	base::flat_map<uint32, Data::LastSpokeTimes> _lastSpoke;
	rpl::event_stream<Group::RejoinEvent> _rejoinEvents;
	rpl::event_stream<> _allowedToSpeakNotifications;
	rpl::event_stream<> _titleChanged;
	base::Timer _lastSpokeCheckTimer;
	base::Timer _checkJoinedTimer;

	crl::time _lastSendProgressUpdate = 0;

	std::shared_ptr<GlobalShortcutManager> _shortcutManager;
	std::shared_ptr<GlobalShortcutValue> _pushToTalk;
	base::Timer _pushToTalkCancelTimer;
	base::Timer _connectingSoundTimer;
	bool _hadJoinedState = false;

	std::unique_ptr<Webrtc::MediaDevices> _mediaDevices;
	QString _audioInputId;
	QString _audioOutputId;
	QString _videoInputId;
	QString _videoDeviceId;

	rpl::lifetime _lifetime;

};

} // namespace Calls
