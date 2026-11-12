-- Manual Attach: a tractor and a plow
-- The same tractor with a plow lined up at its rear hitch, as _G.tractor and _G.plow.

    _G.tractor = StubManualAttach.tractor({})
    _G.plow = StubManualAttach.implement('plow')
    StubManualAttach.lineUp(plow, tractor, 1)
    StubManualAttach.detectAll()
