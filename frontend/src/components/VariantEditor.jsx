import { useCallback, useState } from 'react'
import { Link } from 'react-router-dom'
import { api } from '../services/api'
import { useResource } from '../hooks/useResource'
import { Button, ErrorState, Loader, Modal } from './common/UI'
export default function VariantEditor({ product }) {
  const loader = useCallback(() => api.getVariants(product.id), [product.id])
  const { data, loading, error, reload } = useResource(loader)
  const [label, setLabel] = useState(''),
    [sku, setSku] = useState(''),
    [price, setPrice] = useState(product.price),
    [stock, setStock] = useState(0)
  const [deleting, setDeleting] = useState(null)
  const [busy, setBusy] = useState(false),
    [failure, setFailure] = useState('')
  return (
    <section className="panel form-stack">
      <h2>Product variants</h2>
      <p>
        The original product is the Default option. Each added option has its own SKU, price and
        stock.
      </p>
      {loading ? (
        <Loader />
      ) : error ? (
        <ErrorState message={error} retry={reload} />
      ) : (
        <ul>
          {data.map((v) => (
            <li key={v.id}>
              {v.variantLabel} · {v.sku} · {v.stock} in stock —{' '}
              <Link to={`/admin/products/${v.id}/edit`}>Edit variant</Link>
              <Button
                type="button"
                variant="text"
                disabled={busy}
                onClick={() => {
                  setDeleting(v)
                  setFailure('')
                }}
              >
                Remove variant
              </Button>
            </li>
          ))}
        </ul>
      )}
      <form
        className="form-stack"
        onSubmit={async (e) => {
          e.preventDefault()
          if (busy) return
          setBusy(true)
          setFailure('')
          try {
            await api.createVariant(product.id, {
              name: `${product.name} — ${label}`,
              description: product.description,
              category: product.category,
              imageUrl: product.imageUrl,
              price: Number(price),
              stock: Number(stock),
              variantLabel: label,
              sku,
            })
            setLabel('')
            setSku('')
            await reload()
          } catch (e) {
            setFailure(e.message)
          } finally {
            setBusy(false)
          }
        }}
      >
        <fieldset disabled={busy} className="form-stack">
          <label>
            Variant option
            <input
              required
              maxLength={80}
              placeholder="Blue / Medium"
              value={label}
              onChange={(e) => setLabel(e.target.value)}
            />
          </label>
          <label>
            Variant SKU
            <input
              required
              maxLength={80}
              pattern="[A-Za-z0-9_.-]+"
              value={sku}
              onChange={(e) => setSku(e.target.value.toUpperCase())}
            />
          </label>
          <label>
            Variant price (₹)
            <input
              required
              type="number"
              min="0"
              step="0.01"
              value={price}
              onChange={(e) => setPrice(e.target.value)}
            />
          </label>
          <label>
            Variant stock
            <input
              required
              type="number"
              min="0"
              max="2147483647"
              step="1"
              value={stock}
              onChange={(e) => setStock(e.target.value)}
            />
          </label>
        </fieldset>
        {failure && (
          <p role="alert" className="form-error">
            {failure}
          </p>
        )}
        <Button disabled={busy}>Add variant</Button>
      </form>
      {deleting && (
        <Modal
          title="Remove this variant?"
          confirmLabel="Remove variant"
          busy={busy}
          onClose={() => setDeleting(null)}
          onConfirm={async () => {
            if (busy) return
            setBusy(true)
            setFailure('')
            try {
              await api.deleteProduct(deleting.id)
              setDeleting(null)
              await reload()
            } catch (e) {
              setFailure(e.message)
            } finally {
              setBusy(false)
            }
          }}
        >
          <p>
            {deleting.variantLabel} will be removed from sale. Its stock record is retained for
            existing orders.
          </p>
          {failure && (
            <p role="alert" className="form-error">
              {failure}
            </p>
          )}
        </Modal>
      )}
    </section>
  )
}
