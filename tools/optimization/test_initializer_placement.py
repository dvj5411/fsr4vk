import unittest
from summarize_initializer_placement import check_allocations


def fixture(candidate):
    result=[]
    for role,count,size in [('descriptors',27,8192),('frame_constants',8,2048),('initializers',1,89216)]:
        changed=role=='initializers' and candidate
        result.extend(dict(role=role,prefer_local=int(changed),result=0,allocation_fallback=False,
                           property_flags=7 if changed else 6,selected_type=3 if changed else 2,
                           baseline_type=2,memory_type_bits=15,requested_bytes=size) for _ in range(count))
    return result


class InitializerTests(unittest.TestCase):
    def test_valid(self):
        for mode in (False,True):check_allocations(fixture(mode),mode)
    def test_reject_wrong_role_or_failed_preference(self):
        for key,value in [('prefer_local',0),('property_flags',6),('selected_type',2),
                          ('memory_type_bits',4),('requested_bytes',10),('result',-2),
                          ('allocation_fallback',True)]:
            a=fixture(True);a[-1][key]=value
            with self.assertRaises(ValueError):check_allocations(a,True)
        a=fixture(True);a[0]['selected_type']=3
        with self.assertRaises(ValueError):check_allocations(a,True)
    def test_reject_missing(self):
        with self.assertRaises(ValueError):check_allocations(fixture(True)[:-1],True)
