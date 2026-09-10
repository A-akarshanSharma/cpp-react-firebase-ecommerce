import { useEffect, useState } from 'react'
import { api } from '../../services/api'

const emptyForm = { name: '', price: '', description: '', imageUrl: '', stock: '', category: '' }

export default function AdminProductsPage() {
  const [products, setProducts] = useState([])
  const [loading, setLoading] = useState(true)
  const [showForm, setShowForm] = useState(false)
  const [editingId, setEditingId] = useState(null)
  const [form, setForm] = useState(emptyForm)
  const [error, setError] = useState(null)
  const [saving, setSaving] = useState(false)

  const load = () => {
    setLoading(true)
    api.listProducts().then(setProducts).finally(() => setLoading(false))
  }

  useEffect(load, [])

  const openCreate = () => {
    setEditingId(null)
    setForm(emptyForm)
    setShowForm(true)
    setError(null)
  }

  const openEdit = (product) => {
    setEditingId(product.id)
    setForm({
      name: product.name,
      price: product.price,
      description: product.description,
      imageUrl: product.imageUrl,
      stock: product.stock,
      category: product.category,
    })
    setShowForm(true)
    setError(null)
  }

  const handleSubmit = async (e) => {
    e.preventDefault()
    setSaving(true)
    setError(null)
    const payload = {
      ...form,
      price: parseFloat(form.price) || 0,
      stock: parseInt(form.stock, 10) || 0,
    }
    try {
      if (editingId) {
        await api.updateProduct(editingId, payload)
      } else {
        await api.createProduct(payload)
      }
      setShowForm(false)
      load()
    } catch (err) {
      setError(err.data?.error || err.message)
    } finally {
      setSaving(false)
    }
  }

  const handleDelete = async (product) => {
    if (!window.confirm(`Delete "${product.name}"? This can't be undone.`)) return
    await api.deleteProduct(product.id)
    load()
  }

  return (
    <div>
      <div className="admin-header">
        <h2>Products</h2>
        {!showForm && <button className="btn btn--primary" onClick={openCreate}>Add product</button>}
      </div>

      {showForm && (
        <form className="admin-form" onSubmit={handleSubmit}>
          <label>Name</label>
          <input value={form.name} onChange={(e) => setForm({ ...form, name: e.target.value })} required />

          <div className="admin-form__row">
            <div>
              <label>Price</label>
              <input type="number" step="0.01" value={form.price} onChange={(e) => setForm({ ...form, price: e.target.value })} required />
            </div>
            <div>
              <label>Stock</label>
              <input type="number" value={form.stock} onChange={(e) => setForm({ ...form, stock: e.target.value })} required />
            </div>
          </div>

          <label>Category</label>
          <input value={form.category} onChange={(e) => setForm({ ...form, category: e.target.value })} />

          <label>Image URL</label>
          <input value={form.imageUrl} onChange={(e) => setForm({ ...form, imageUrl: e.target.value })} placeholder="https://…" />

          <label>Description</label>
          <textarea value={form.description} onChange={(e) => setForm({ ...form, description: e.target.value })} />

          {error && <p className="error-text" style={{ marginTop: 12 }}>{error}</p>}

          <div className="admin-form__actions">
            <button className="btn btn--primary" disabled={saving}>{saving ? 'Saving…' : editingId ? 'Save changes' : 'Create product'}</button>
            <button type="button" className="btn btn--ghost" onClick={() => setShowForm(false)}>Cancel</button>
          </div>
        </form>
      )}

      {loading ? (
        <p className="meta">Loading…</p>
      ) : (
        <table className="admin-table">
          <thead>
            <tr>
              <th></th>
              <th>Name</th>
              <th>Category</th>
              <th>Price</th>
              <th>Stock</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            {products.map((p) => (
              <tr key={p.id}>
                <td>{p.imageUrl ? <img className="admin-table__thumb" src={p.imageUrl} alt="" /> : <div className="admin-table__thumb" />}</td>
                <td>{p.name}</td>
                <td className="meta">{p.category}</td>
                <td className="admin-table__num">₹{p.price.toFixed(2)}</td>
                <td className="admin-table__num">
                  {p.stock}
                  {p.stock <= 3 && <span className="admin-badge admin-badge--low" style={{ marginLeft: 8 }}>Low</span>}
                </td>
                <td>
                  <button className="admin-link-btn" onClick={() => openEdit(p)} style={{ marginRight: 16 }}>Edit</button>
                  <button className="admin-link-btn admin-link-btn--danger" onClick={() => handleDelete(p)}>Delete</button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}

      {!loading && products.length === 0 && !showForm && (
        <div className="empty-state">
          <h3>No products yet</h3>
          <p>Add your first product to get the shop started.</p>
        </div>
      )}
    </div>
  )
}
