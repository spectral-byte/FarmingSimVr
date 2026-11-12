-- Manual Attach: a tractor with a plow attached
-- The same tractor with a plow that has a PTO and hoses already attached at its rear hitch, as _G.tractor and _G.plow (the PTO and the hoses are
-- not connected yet).

    _G.tractor = StubManualAttach.tractor({})
    _G.plow = StubManualAttach.implement('plow', { hasPto = true, hasHoses = true })
    StubManualAttach.lineUp(plow, tractor, 1)
    StubManualAttach.attach(tractor, plow, 1, 1)
    StubManualAttach.detectAll()
