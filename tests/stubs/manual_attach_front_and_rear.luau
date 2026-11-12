-- Manual Attach: a tractor with an Implement at each hitch
-- A tractor with an Implement lined up at each hitch: the rear one 2 m ahead (where the ray of kHandX runs), the front one nearer to the player and
-- to the side (where the ray of a hand 0.8 m to the side runs, at world -1.2, -0.2, 1.0), so Manual Attach's own nearest candidate is the front one.
-- _G.tractor, _G.rearPlow (joint 1), _G.frontLoader (joint 2).

    _G.tractor = StubManualAttach.tractor({ rear = { -0.4, -0.2, 2.0 }, front = { -1.2, -0.2, 1.0 } })
    _G.rearPlow = StubManualAttach.implement('rearPlow')
    _G.frontLoader = StubManualAttach.implement('frontLoader')
    StubManualAttach.lineUp(rearPlow, tractor, 1)
    StubManualAttach.lineUp(frontLoader, tractor, 2)
    StubManualAttach.detectAll()
