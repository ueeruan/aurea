import Foundation

// Compile this fixture with the actual AiRewardFlow.swift, without UIKit/ads SDKs.
struct AiRequest: Equatable {}

@MainActor
private final class FakeAds: RewardedAds {
    var isReady = true
    var loads: [() -> Void] = []
    var rewards: [() -> Void] = []
    var opens: [() -> Void] = []
    func ready() -> Bool { isReady }
    func load(loaded: @escaping () -> Void, failed: @escaping (String) -> Void) { loads.append(loaded) }
    func show(opened: @escaping () -> Void, reward: @escaping () -> Void,
              closed: @escaping () -> Void, failed: @escaping (String) -> Void) -> Bool {
        opens.append(opened); rewards.append(reward); opened(); return true
    }
}

@MainActor
private final class Fixture {
    let ads = FakeAds()
    var tickets: [String: (String) -> Void] = [:]
    var bound: [String] = []
    var finishes: [AiRewardFlow.Finish] = []
    var last: AiGenerationSession?
    lazy var flow = AiRewardFlow(ads: ads,
        requestTicket: { [unowned self] session, ok, _ in self.tickets[session.generationId] = ok },
        bindAd: { [unowned self] in self.bound.append($0) },
        startGeneration: { [unowned self] _, _, finish in self.finishes.append(finish) },
        onChange: { [unowned self] in self.last = $0 })
    func generate(_ id: String) { flow.generate(AiRequest(), id: id) }
    func ticket(_ id: String) { tickets[id]?("ticket-" + id) }
}

@main
private struct SessionChecks {
    @MainActor static func main() {
        let ticket = Fixture()
        ticket.generate("old"); ticket.generate("new"); ticket.ticket("old")
        precondition(ticket.bound.isEmpty && ticket.ads.rewards.isEmpty, "late ticket presented an old ad")
        ticket.ticket("new")
        precondition(ticket.bound == ["ticket-new"] && ticket.ads.rewards.count == 1)

        let load = Fixture()
        load.ads.isReady = false
        load.generate("old"); load.ticket("old")
        load.generate("new"); load.ticket("new")
        load.ads.loads[0]()
        precondition(load.ads.rewards.isEmpty, "late ad load presented old session")
        load.ads.loads[1]()
        precondition(load.ads.rewards.count == 1)

        let reward = Fixture()
        reward.generate("old"); reward.ticket("old")
        reward.generate("new"); reward.ticket("new")
        reward.ads.opens[0](); reward.ads.rewards[0]()
        precondition(reward.finishes.isEmpty && reward.last?.generationId == "new", "late reward started old job")
        reward.ads.rewards[1](); reward.ads.rewards[1]()
        precondition(reward.finishes.count == 1, "one reward must start exactly one generation")

        reward.finishes[0](nil, "sem_conexao", true)
        reward.flow.retryWithoutAd("new")
        precondition(reward.finishes.count == 2)
        let file = URL(fileURLWithPath: "/tmp/result.mp4")
        reward.finishes[0](file, nil, false)
        precondition(reward.last?.status == .generating && reward.last?.result == nil,
                     "completion of old attempt unlocked a retry")
        reward.finishes[1](file, nil, false)
        precondition(reward.last?.status == .unlocked && reward.last?.result == file)
        print("PASS: late ticket, late ad load, late reward/open, duplicate reward, stale retry completion")
    }
}
