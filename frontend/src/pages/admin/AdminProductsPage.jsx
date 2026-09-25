import { useRef, useState } from 'react'
import { Link } from 'react-router-dom'
import { api } from '../../services/api'
import { useCatalog } from '../../context/CatalogContext'
import { useToast } from '../../context/ToastContext'
import {
  Button,
  EmptyState,
  ErrorState,
  Loader,
  Modal,
  PageHeading,
  ProductImage,
} from '../../components/common/UI'
import { money } from '../../utils/format'
export default function AdminProductsPage() {
  const { products, loading, error, reload } = useCatalog(),
    notify = useToast()
  const [query, setQuery] = useState(''),
    [deleting, setDeleting] = useState(null),
    [busy, setBusy] = useState(false),
    [failure, setFailure] = useState(''),
    lock = useRef(false)
  const remove = async () => {
    if (lock.current) return
    lock.current = true
    setBusy(true)
    setFailure('')
    try {
      await api.deleteProduct(deleting.id)
      setDeleting(null)
      notify('Product deleted.')
      await reload()
    } catch (e) {
      setFailure(e.message)
    } finally {
      lock.current = false
      setBusy(false)
    }
  }
  const filtered = products.filter((p) =>
    `${p.name} ${p.category}`.toLowerCase().includes(query.toLowerCase()),
  )
  return (
    <>
      <PageHeading
        eyebrow="THE COLLECTION"
        title="Products"
        action={
          <Link className="btn primary" to="/admin/products/new">
            + Add product
          </Link>
        }
      >
        Manage the details behind every good find.
      </PageHeading>
      <div className="panel">
        <div className="table-toolbar">
          <label className="search-label">
            <span className="sr-only">Search products</span>
            <input
              type="search"
              placeholder="Search products or categories…"
              value={query}
              onChange={(e) => setQuery(e.target.value)}
            />
          </label>
          <span className="muted">{filtered.length} products</span>
        </div>
        {loading ? (
          <Loader />
        ) : error ? (
          <ErrorState message={error} retry={reload} />
        ) : !filtered.length ? (
          <EmptyState
            title={products.length ? 'No matching products' : 'Start your collection'}
            to={!products.length ? '/admin/products/new' : undefined}
            action="Add your first product"
          >
            {products.length
              ? 'Try a different search.'
              : 'Add a product to bring your storefront to life.'}
          </EmptyState>
        ) : (
          <div className="table-scroll">
            <table>
              <thead>
                <tr>
                  <th>Product</th>
                  <th>Category</th>
                  <th>Price</th>
                  <th>Stock</th>
                  <th>Actions</th>
                </tr>
              </thead>
              <tbody>
                {filtered.map((p) => (
                  <tr key={p.id}>
                    <td>
                      <div className="table-product">
                        <ProductImage src={p.imageUrl} name={p.name} />
                        <strong>{p.name}</strong>
                      </div>
                    </td>
                    <td>{p.category || '—'}</td>
                    <td>{money(p.price)}</td>
                    <td>
                      <span
                        className={p.stock <= 0 ? 'form-error' : p.stock <= 3 ? 'low-stock' : ''}
                      >
                        {p.stock <= 0 ? 'Out of stock' : `${p.stock} available`}
                      </span>
                    </td>
                    <td>
                      <div className="actions">
                        <Link
                          className="inline-link"
                          to={`/admin/products/${encodeURIComponent(p.id)}/edit`}
                        >
                          Edit
                        </Link>
                        <Button
                          variant="text"
                          className="danger-text"
                          onClick={() => {
                            setDeleting(p)
                            setFailure('')
                          }}
                        >
                          Delete
                        </Button>
                      </div>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
      {deleting && (
        <Modal
          title="Delete this product?"
          confirmLabel="Delete product"
          onClose={() => setDeleting(null)}
          onConfirm={remove}
          busy={busy}
          danger
        >
          <p>
            “{deleting.name}” will be removed from the storefront. Its inventory record is retained
            for existing orders and cancellations.
          </p>
          {failure && (
            <p className="form-error" role="alert">
              {failure}
            </p>
          )}
        </Modal>
      )}
    </>
  )
}
