import { useCallback, useEffect, useRef, useState } from 'react'
import { Link, useNavigate, useParams } from 'react-router-dom'
import { api } from '../../services/api'
import { useResource } from '../../hooks/useResource'
import { useCatalog } from '../../context/CatalogContext'
import { useToast } from '../../context/ToastContext'
import { Button, ErrorState, Loader, PageHeading, ProductImage } from '../../components/common/UI'
import VariantEditor from '../../components/VariantEditor'
import StockAdjustment from '../../components/StockAdjustment'
import { prepareImage } from '../../utils/media'
const blank = { name: '', description: '', category: '', price: '', stock: '', imageUrl: '' }
export default function AdminProductFormPage() {
  const { id } = useParams(),
    navigate = useNavigate(),
    notify = useToast(),
    { reload, categories } = useCatalog()
  const load = useCallback(() => (id ? api.getProduct(id) : Promise.resolve(blank)), [id])
  const { data, loading, error, reload: retry } = useResource(load)
  const [form, setForm] = useState(blank),
    [busy, setBusy] = useState(false),
    [failure, setFailure] = useState(''),
    lock = useRef(false)
  useEffect(() => {
    if (data) setForm(Object.fromEntries(Object.keys(blank).map((k) => [k, data[k]])))
  }, [data])
  const set = (key, value) => setForm((f) => ({ ...f, [key]: value }))
  const save = async (e) => {
    e.preventDefault()
    if (lock.current) return
    setFailure('')
    const payload = Object.fromEntries(
      Object.entries(form).map(([k, v]) => [k, typeof v === 'string' ? v.trim() : v]),
    )
    payload.price = Number(payload.price)
    payload.stock = Number(payload.stock)
    if (
      !payload.name ||
      !payload.description ||
      !payload.category ||
      !Number.isFinite(payload.price) ||
      payload.price < 0 ||
      !Number.isInteger(payload.stock) ||
      payload.stock < 0 ||
      payload.stock > 2147483647
    ) {
      setFailure(
        'Complete the required fields. Price must be nonnegative and stock must be a whole number between 0 and 2,147,483,647.',
      )
      return
    }
    if (
      payload.imageUrl &&
      !/^https?:\/\//.test(payload.imageUrl) &&
      !/^\/media\/[a-f0-9]{64}\.png$/.test(payload.imageUrl)
    ) {
      setFailure('Use an image URL beginning with https:// or http://.')
      return
    }
    lock.current = true
    setBusy(true)
    try {
      if (id) {
        delete payload.stock
        payload.version = data.version
      }
      await (id ? api.updateProduct(id, payload) : api.createProduct(payload))
      notify(id ? 'Product updated.' : 'Product created.')
      await reload()
      navigate('/admin/products')
    } catch (e) {
      setFailure(e.message)
    } finally {
      lock.current = false
      setBusy(false)
    }
  }
  if (loading) return <Loader />
  if (error) return <ErrorState message={error} retry={retry} />
  return (
    <>
      <Link to="/admin/products" className="text-link">
        ← Back to products
      </Link>
      <PageHeading
        eyebrow="PRODUCT DETAILS"
        title={id ? 'Edit your good find.' : 'Add something good.'}
      />
      <form onSubmit={save} className="product-form-layout">
        <section className="panel form-stack">
          <fieldset disabled={busy} className="form-stack">
            <label>
              Product name
              <input
                required
                maxLength={160}
                value={form.name}
                onChange={(e) => set('name', e.target.value)}
              />
            </label>
            <label>
              Description
              <textarea
                required
                rows={5}
                maxLength={5000}
                value={form.description}
                onChange={(e) => set('description', e.target.value)}
              />
            </label>
            <label>
              Category
              <input
                required
                aria-label="Category"
                list="existing-categories"
                maxLength={80}
                value={form.category}
                onChange={(e) => set('category', e.target.value)}
              />
              <datalist id="existing-categories">
                {categories.map((c) => (
                  <option key={c} value={c} />
                ))}
              </datalist>
            </label>
            <div className="form-row">
              <label>
                Price (₹)
                <input
                  type="number"
                  min="0"
                  step="0.01"
                  required
                  value={form.price}
                  onChange={(e) => set('price', e.target.value)}
                />
              </label>
              {!id && (
                <label>
                  Stock quantity
                  <input
                    type="number"
                    min="0"
                    max="2147483647"
                    step="1"
                    required
                    value={form.stock}
                    onChange={(e) => set('stock', e.target.value)}
                  />
                </label>
              )}
            </div>
            <label>
              Image URL <span className="muted">(optional)</span>
              <input
                type="text"
                maxLength={2048}
                placeholder="https://example.com/product.jpg"
                value={form.imageUrl}
                onChange={(e) => set('imageUrl', e.target.value)}
              />
            </label>
            <label>
              Upload product image
              <input
                type="file"
                accept="image/png,image/jpeg,image/webp"
                onChange={async (e) => {
                  const file = e.target.files?.[0]
                  if (!file || lock.current) return
                  lock.current = true
                  setBusy(true)
                  setFailure('')
                  try {
                    const result = await api.uploadImage(await prepareImage(file))
                    set('imageUrl', result.url)
                  } catch (e) {
                    setFailure(e.message)
                  } finally {
                    lock.current = false
                    setBusy(false)
                  }
                }}
              />
            </label>
            <p className="field-help">
              Use a publicly accessible image URL. A studio placeholder is shown if no image is
              available.
            </p>
          </fieldset>
          {failure && (
            <div>
              <p className="form-error" role="alert">
                {failure}
              </p>
              {id && (
                <Button
                  type="button"
                  variant="secondary"
                  disabled={busy}
                  onClick={() => {
                    setFailure('')
                    retry()
                  }}
                >
                  Reload latest product (discard edits)
                </Button>
              )}
            </div>
          )}
          <div className="actions">
            <Button disabled={busy}>
              {busy ? 'Saving…' : id ? 'Save changes' : 'Create product'}
            </Button>
            <Link
              className={`btn secondary ${busy ? 'disabled-link' : ''}`}
              to="/admin/products"
              onClick={(e) => busy && e.preventDefault()}
            >
              Cancel
            </Link>
          </div>
        </section>
        <aside className="panel product-preview">
          <p className="eyebrow">IMAGE PREVIEW</p>
          <ProductImage src={form.imageUrl} name={form.name || 'Your product'} />
          <h2>{form.name || 'Your next good find'}</h2>
          <p className="muted">{form.category || 'Product category'}</p>
        </aside>
      </form>
      {id && <StockAdjustment key={id} id={id} />}
      {id && data && !data.parentProductId && <VariantEditor product={data} />}
      {data?.parentProductId && (
        <Link className="text-link" to={`/admin/products/${data.parentProductId}/edit`}>
          Back to parent product
        </Link>
      )}
    </>
  )
}
