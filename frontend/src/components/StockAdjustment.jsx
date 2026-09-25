import { useCallback, useRef, useState } from 'react'
import { api } from '../services/api'
import { useResource } from '../hooks/useResource'
import { useCatalog } from '../context/CatalogContext'
import { Button, ErrorState, Loader } from './common/UI'

export default function StockAdjustment({ id }) {
  const load = useCallback(() => api.getProduct(id), [id])
  const { data, loading, error, reload } = useResource(load)
  const { reload: reloadCatalog } = useCatalog()
  const [delta, setDelta] = useState(''),
    [reason, setReason] = useState('')
  const [busy, setBusy] = useState(false),
    [failure, setFailure] = useState(''),
    [message, setMessage] = useState('')
  const lock = useRef(false)
  const [reviewRequired, setReviewRequired] = useState(false)
  const refresh = async () => {
    setFailure('')
    const result = await reload()
    if (result) setReviewRequired(false)
  }
  const submit = async (event) => {
    event.preventDefault()
    if (lock.current || reviewRequired) return
    lock.current = true
    setBusy(true)
    setFailure('')
    setMessage('')
    try {
      await api.adjustStock(id, {
        delta: Number(delta),
        reason: reason.trim(),
        version: data.version,
      })
      setDelta('')
      setReason('')
      setMessage('Stock adjusted. Reload product details before saving any open edits.')
      await reload()
      await reloadCatalog()
    } catch (e) {
      setFailure(e.message)
      // An unconfirmed request may have committed. Require a fresh inventory review before retrying.
      setReviewRequired(true)
    } finally {
      lock.current = false
      setBusy(false)
    }
  }
  return (
    <section className="panel form-stack">
      <h2>Adjust stock</h2>
      {loading ? (
        <Loader />
      ) : error ? (
        <ErrorState message={error} retry={refresh} />
      ) : (
        <form className="form-stack" onSubmit={submit}>
          <p>
            Current stock: <strong>{data.stock}</strong>
          </p>
          <p className="field-help">
            Add received units or subtract damaged or missing units. Product detail edits do not
            change stock.
          </p>
          <fieldset disabled={busy || reviewRequired} className="form-stack">
            <label>
              Stock change
              <input
                required
                type="number"
                step="1"
                min="-2147483647"
                max="2147483647"
                value={delta}
                onChange={(e) => setDelta(e.target.value)}
                placeholder="5 to add, -2 to remove"
              />
            </label>
            <label>
              Adjustment reason
              <input
                required
                maxLength={500}
                value={reason}
                onChange={(e) => setReason(e.target.value)}
                placeholder="Received new stock"
              />
            </label>
            {delta !== '' && Number.isInteger(Number(delta)) && (
              <p>Stock after adjustment: {data.stock + Number(delta)}</p>
            )}
            <Button
              disabled={
                !Number.isInteger(Number(delta)) ||
                Number(delta) === 0 ||
                !reason.trim() ||
                data.stock + Number(delta) < 0 ||
                data.stock + Number(delta) > 2147483647
              }
            >
              Apply stock adjustment
            </Button>
          </fieldset>
        </form>
      )}
      {failure && (
        <p className="form-error" role="alert">
          {failure}
        </p>
      )}
      {message && <p role="status">{message}</p>}
      {!loading && (
        <Button type="button" variant="secondary" disabled={busy} onClick={refresh}>
          Reload current stock
        </Button>
      )}
    </section>
  )
}
