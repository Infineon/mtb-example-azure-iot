from base64 import b64encode, b64decode
from hashlib import sha256
from time import time
from urllib import parse
from hmac import HMAC

def generate_sas_token(uri, key, policy_name, expiry=3600):
    ttl = time() + expiry
    sign_key = "%s\n%d" % ((parse.quote_plus(uri)), int(ttl))
    print(sign_key)
    signature = b64encode(HMAC(b64decode(key), sign_key.encode('utf-8'), sha256).digest())

    rawtoken = {
        'sr' : uri,
        'sig': signature,
        'se' : str(int(ttl))
    }

    if policy_name is not None:
        rawtoken['skn'] = policy_name
    print ('SharedAccessSignature ' + parse.urlencode(rawtoken))
    return 'SharedAccessSignature ' + parse.urlencode(rawtoken)

#Fill Scope ID, Registration ID and Provisioning SAS Key below to generate SAS token for DPS application.
#SAS token expiry time will be 7200 minutes.Exapmle - generate_sas_token("0ne... 2F/registrations/azurea...regID1", "RfD ... ==" , None, expiry=7200)
generate_sas_token("{Scope ID}/registrations/{Registration ID}", "{Provisioning SAS Key}" , None, expiry=7200)
