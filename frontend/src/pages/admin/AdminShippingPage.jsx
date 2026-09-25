import { useEffect, useState } from 'react'
import { api } from '../../services/api'
import { useResource } from '../../hooks/useResource'
import { Button, ErrorState, Loader, PageHeading } from '../../components/common/UI'
export default function AdminShippingPage() {
  const { data, loading, error, reload } = useResource(api.getShippingSettings)
  const [methods, setMethods] = useState([]),
    [busy, setBusy] = useState(false),
    [failure, setFailure] = useState(''),
    [saved, setSaved] = useState(false)
  useEffect(() => {
    if (data) setMethods(data.methods.map((m) => ({ ...m, countriesText: m.countries.join(', ') })))
  }, [data])
  const update = (index, field, value) => {
    setMethods((rows) => rows.map((m, i) => (i === index ? { ...m, [field]: value } : m)))
    setSaved(false)
  }
  if (loading) return <Loader />
  if (error) return <ErrorState message={error} retry={reload} />
  return (
    <>
      <PageHeading eyebrow="STORE OPERATIONS" title="Shipping methods">
        Set delivery prices, destinations and estimated delivery times.
      </PageHeading>
      <form
        className="form-stack"
        onSubmit={async (e) => {
          e.preventDefault()
          if (busy) return
          setBusy(true)
          setFailure('')
          setSaved(false)
          try {
            await api.saveShippingSettings({
              version: data.version,
              methods: methods.map(({ countriesText, ...m }) => ({
                ...m,
                fee: Number(m.fee),
                countries: countriesText
                  .split(',')
                  .map((c) => c.trim().toUpperCase())
                  .filter(Boolean),
              })),
            })
            await reload()
            setSaved(true)
          } catch (e) {
            setFailure(e.message)
          } finally {
            setBusy(false)
          }
        }}
      >
        <p className="notice">
          Active methods are offered at checkout. Leave countries blank to serve all countries. If
          no methods are active, checkout is disabled. Existing order prices remain unchanged.
        </p>
        {methods.map((m, i) => (
          <fieldset className="panel form-stack" disabled={busy} key={m.id}>
            <legend>Method {i + 1}</legend>
            <label>
              Fulfillment type
              <select
                value={m.kind || 'delivery'}
                onChange={(e) => update(i, 'kind', e.target.value)}
              >
                <option value="delivery">Delivery</option>
                <option value="pickup">Store pickup</option>
              </select>
            </label>
            {m.kind === 'pickup' && (
              <label>
                Pickup location and instructions
                <input
                  required
                  maxLength={500}
                  value={m.pickupInstructions || ''}
                  onChange={(e) => update(i, 'pickupInstructions', e.target.value)}
                />
              </label>
            )}
            <label>
              Name
              <input
                required
                maxLength={100}
                value={m.name}
                onChange={(e) => update(i, 'name', e.target.value)}
              />
            </label>
            <label>
              Delivery fee (₹)
              <input
                required
                type="number"
                min="0"
                max="10000000000"
                step="0.01"
                value={m.fee}
                onChange={(e) => update(i, 'fee', e.target.value)}
              />
            </label>
            <label>
              Country codes (comma separated)
              <input
                placeholder="IN, US"
                value={m.countriesText}
                onChange={(e) => update(i, 'countriesText', e.target.value)}
              />
            </label>
            <label>
              Estimated delivery
              <input
                maxLength={80}
                placeholder="3–5 business days"
                value={m.estimatedDays}
                onChange={(e) => update(i, 'estimatedDays', e.target.value)}
              />
            </label>
            <label className="checkbox">
              <input
                type="checkbox"
                checked={m.active}
                onChange={(e) => update(i, 'active', e.target.checked)}
              />
              Active
            </label>
            <Button
              type="button"
              variant="text"
              onClick={() => setMethods((rows) => rows.filter((_, index) => index !== i))}
            >
              Remove method
            </Button>
          </fieldset>
        ))}
        <Button
          type="button"
          variant="secondary"
          disabled={busy || methods.length >= 20}
          onClick={() =>
            setMethods([
              ...methods,
              {
                id: crypto.randomUUID(),
                name: '',
                kind: 'delivery',
                pickupInstructions: '',
                fee: 0,
                countriesText: '',
                active: true,
                estimatedDays: '',
              },
            ])
          }
        >
          Add shipping method
        </Button>
        {failure && (
          <p role="alert" className="form-error">
            {failure}
          </p>
        )}
        {saved && <p role="status">Shipping settings saved.</p>}
        <div className="actions">
          <Button disabled={busy}>Save shipping settings</Button>
          <Button type="button" variant="text" disabled={busy} onClick={reload}>
            Reload settings
          </Button>
        </div>
      </form>
    </>
  )
}
