import schema

import aioxmpp

import hintlib.sample
import hintlib.xso


class _JIDValidator:
    @staticmethod
    def validate(v):
        return aioxmpp.JID.fromstr(v)


jid = _JIDValidator()


def wrap_batch(batch: hintlib.sample.SampleBatch):
    payload = hintlib.xso.SampleBatch()
    payload.timestamp = batch.timestamp
    payload.part = batch.bare_path.part
    payload.instance = batch.bare_path.instance

    for subpart, value in batch.samples.items():
        sample_xso = hintlib.xso.NumericSample()
        sample_xso.subpart = subpart
        sample_xso.value = value
        payload.samples.append(sample_xso)

    return payload
