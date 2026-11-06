"""gen.py's descriptors for the shapes that once went out as one element (host heap overflows): typed arrays whose
count is behind a pointer, and parallel arrays sharing one later count; and single values that must stay single.
   gen-check.py <lsteamclient dir>   Prints a line per case and exits 1 on a failure."""
import os
import re
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
out = tempfile.mkdtemp()
subprocess.run([sys.executable, '-I', os.path.join(here, '..', 'gen.py'), sys.argv[1], out], check=True,
               stdout=subprocess.DEVNULL)
text = open(os.path.join(out, 'descriptors.c')).read()


def field(call, name):
    """kind, flags and the count's field name of call's pointer `name` (None for no count)."""
    m = re.search(r'\{ offsetof\(struct ' + call + r'_params, ' + name + r'\), (MN_\w+), ([^,]+), [^,]+, '
                  r'(?:offsetof\(struct ' + call + r'_params, (\w+)\)|0)', text)
    return (m.group(1), m.group(2).strip(), m.group(3)) if m else None


V = 'ISteamInventory_STEAMINVENTORY_INTERFACE_V003_'
cases = [
    ('GetResultItems items, count behind a pointer', V + 'GetResultItems', 'pOutItemsArray',
     ('MN_BUF', 'MN_COUNT_PTR', 'punOutItemsArraySize')),
    ('GetItemDefinitionIDs ids, count behind a pointer', V + 'GetItemDefinitionIDs', 'pItemDefIDs',
     ('MN_BUF', 'MN_COUNT_PTR', 'punItemDefIDsArraySize')),
    ('GetEligiblePromoItemDefinitionIDs ids', V + 'GetEligiblePromoItemDefinitionIDs', 'pItemDefIDs',
     ('MN_BUF', 'MN_COUNT_PTR', 'punItemDefIDsArraySize')),
    ('GetItemsWithPrices defs', V + 'GetItemsWithPrices', 'pArrayItemDefs', ('MN_BUF', '0', 'unArrayLength')),
    ('GetItemsWithPrices current prices', V + 'GetItemsWithPrices', 'pCurrentPrices', ('MN_BUF', '0', 'unArrayLength')),
    ('GetItemsWithPrices base prices', V + 'GetItemsWithPrices', 'pBasePrices', ('MN_BUF', '0', 'unArrayLength')),
    ('GenerateItems defs', V + 'GenerateItems', 'pArrayItemDefs', ('MN_BUF', 'MN_IN_ONLY', 'unArrayLength')),
    ('ExchangeItems generate', V + 'ExchangeItems', 'pArrayGenerate', ('MN_BUF', 'MN_IN_ONLY', 'unArrayGenerateLength')),
    ('ExchangeItems destroy', V + 'ExchangeItems', 'pArrayDestroy', ('MN_BUF', 'MN_IN_ONLY', 'unArrayDestroyLength')),
    ('TradeItems give', V + 'TradeItems', 'pArrayGive', ('MN_BUF', 'MN_IN_ONLY', 'nArrayGiveLength')),
    ('TradeItems get', V + 'TradeItems', 'pArrayGet', ('MN_BUF', 'MN_IN_ONLY', 'nArrayGetLength')),
    ('StartPurchase defs', V + 'StartPurchase', 'pArrayItemDefs', ('MN_BUF', 'MN_IN_ONLY', 'unArrayLength')),
    ('GenerateItems result handle stays one', V + 'GenerateItems', 'pResultHandle', ('MN_FIXED', '0', None)),
    ('ExchangeItems result handle stays one', V + 'ExchangeItems', 'pResultHandle', ('MN_FIXED', '0', None)),
]
failures = 0
for what, call, name, want in cases:
    got = field(call, name)
    if got and got[0] == want[0] and want[1] in got[1] and got[2] == want[2]:
        print(f'ok   {what}')
    else:
        print(f'FAIL {what}: {got}, want {want}')
        failures += 1
    # The WoW64 table's twin: the same shape, its pointers 4 bytes (MN_PTR32).
    got = field('wow64_' + call, name)
    if got and got[0] == want[0] and 'MN_PTR32' in got[1] and (want[1] == '0' or want[1] in got[1]) and got[2] == want[2]:
        print(f'ok   {what} (wow64)')
    else:
        print(f'FAIL {what} (wow64): {got}, want {want} with MN_PTR32')
        failures += 1
# A W32_PTR to an array (char (*errMsg)[1024]): its pointee is the array, not a pointer's 8 bytes (M3's review).
call = 'wow64_ISteamNetworkingSockets_SteamNetworkingSockets012_SetCertificate'
m = re.search(r'\{ offsetof\(struct ' + call + r'_params, errMsg\), MN_FIXED, [^,]+, ([^}]*?), 0, 0, 0, 0, 0 \}', text)
size = m.group(1) if m else None
ok = size == 'sizeof(*(char (*)[1024])0)'
print(('ok   ' if ok else 'FAIL ') + f'wow64 SetCertificate errMsg sized as its array: {size}')
failures += not ok
# steamclient_next_callback's size is a capacity in and the needed size out (MN_CAP), in both tables.
for c in ('steamclient_next_callback', 'wow64_steamclient_next_callback'):
    got = field(c, 'callback')
    ok = bool(got) and 'MN_CAP' in got[1]
    print(('ok   ' if ok else 'FAIL ') + f'{c} callback MN_CAP: {got}')
    failures += not ok
print('gen-check: ' + ('FAIL' if failures else 'all passed'))
sys.exit(1 if failures else 0)
