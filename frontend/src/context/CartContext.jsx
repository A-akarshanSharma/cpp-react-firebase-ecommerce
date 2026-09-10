import { createContext, useContext, useEffect, useState, useCallback } from 'react'
import { api } from '../services/api'
import { useAuth } from './AuthContext'

const CartContext = createContext(null)

export function CartProvider({ children }) {
  const { user } = useAuth()
  const [items, setItems] = useState([])
  const [loading, setLoading] = useState(false)

  const refresh = useCallback(async () => {
    if (!user) {
      setItems([])
      return
    }
    setLoading(true)
    try {
      const cart = await api.getCart()
      setItems(cart)
    } finally {
      setLoading(false)
    }
  }, [user])

  useEffect(() => {
    refresh()
  }, [refresh])

  // Every mutation returns {ok, error} instead of throwing, so pages can show
  // a friendly message (e.g. "only 3 left") without a try/catch at every call site.
  const addItem = async (productId, quantity = 1) => {
    try {
      await api.addToCart(productId, quantity)
      await refresh()
      return { ok: true }
    } catch (err) {
      return { ok: false, error: err.data?.error || err.message, available: err.data?.available }
    }
  }

  const updateItem = async (productId, quantity) => {
    try {
      await api.updateCartItem(productId, quantity)
      await refresh()
      return { ok: true }
    } catch (err) {
      return { ok: false, error: err.data?.error || err.message, available: err.data?.available }
    }
  }

  const removeItem = async (productId) => {
    await api.removeFromCart(productId)
    await refresh()
  }

  const itemCount = items.reduce((sum, item) => sum + item.quantity, 0)
  const total = items.reduce((sum, item) => sum + item.price * item.quantity, 0)

  return (
    <CartContext.Provider value={{ items, loading, itemCount, total, addItem, updateItem, removeItem, refresh }}>
      {children}
    </CartContext.Provider>
  )
}

export function useCart() {
  return useContext(CartContext)
}
