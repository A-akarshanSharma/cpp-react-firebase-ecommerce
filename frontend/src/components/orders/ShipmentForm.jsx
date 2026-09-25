import { useState } from 'react'
import { api } from '../../services/api'
import { Button } from '../common/UI'
export default function ShipmentForm({ order, onSaved }) {
  const [form, setForm] = useState({
    carrier: order.shipment?.carrier || '',
    trackingNumber: order.shipment?.trackingNumber || '',
    trackingUrl: order.shipment?.trackingUrl || '',
  })
  const [busy, setBusy] = useState(false),
    [error, setError] = useState('')
  return (
    <form
      className="panel form-stack"
      onSubmit={async (e) => {
        e.preventDefault()
        if (busy) return
        setBusy(true)
        setError('')
        try {
          await api.saveShipment(order.id, {
            ...form,
            version: order.shipment?.version || 'initial',
          })
          await onSaved()
        } catch (e) {
          setError(e.message)
        } finally {
          setBusy(false)
        }
      }}
    >
      <h2>Shipping and tracking</h2>
      <p>Book delivery with your carrier, then record its tracking details here.</p>
      <fieldset disabled={busy} className="form-stack">
        {[
          ['carrier', 'Carrier', 100],
          ['trackingNumber', 'Tracking number', 160],
          ['trackingUrl', 'Tracking URL (optional)', 2048],
        ].map(([key, label, max]) => (
          <label key={key}>
            {label}
            <input
              maxLength={max}
              required={key !== 'trackingUrl'}
              type={key === 'trackingUrl' ? 'url' : 'text'}
              value={form[key]}
              onChange={(e) => setForm({ ...form, [key]: e.target.value })}
            />
          </label>
        ))}
      </fieldset>
      {error && (
        <p role="alert" className="form-error">
          {error}
        </p>
      )}
      <Button disabled={busy}>Save tracking</Button>
    </form>
  )
}
