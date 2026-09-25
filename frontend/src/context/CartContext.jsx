import { createContext, useContext, useEffect, useRef, useState, useCallback } from 'react'
import { api } from '../services/api'
import { useAuth } from './AuthContext'
import { useToast } from './ToastContext'
import { checkoutAttempt, clearCheckoutAttempt } from '../services/checkoutAttempt'
const Context = createContext(null)
export function CartProvider({ children }) {
  const { profile } = useAuth()
  const uid = profile?.uid
  const currentUid = useRef(uid)
  currentUid.current = uid
  const sequence = useRef(0)
  const lock = useRef(false)
  const [items, setItems] = useState([])
  const [loading, setLoading] = useState(false)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const notify = useToast()
  const refresh = useCallback(async () => {
    const request = ++sequence.current
    if (!uid) {
      setItems([])
      setLoading(false)
      setError('')
      return []
    }
    setLoading(true)
    setError('')
    try {
      const data = await api.getCart()
      if (request === sequence.current && uid === currentUid.current) setItems(data)
      return data
    } catch (e) {
      if (request === sequence.current) setError(e.message)
      return null
    } finally {
      if (request === sequence.current) setLoading(false)
    }
  }, [uid])
  useEffect(() => {
    setItems([])
    refresh()
    return () => {
      ++sequence.current
    }
  }, [refresh])
  const mutate = async (action, message) => {
    if (lock.current || !uid) return false
    lock.current = true
    setBusy(true)
    try {
      await action()
      if (uid !== currentUid.current) return false
      const data = await refresh()
      notify(
        data ? message : `${message} Refresh the cart to see the latest details.`,
        data ? 'success' : 'error',
      )
      return true
    } catch (e) {
      notify(
        e.data?.available !== undefined
          ? `${e.message}. Available stock: ${e.data.available}.`
          : e.message,
        'error',
      )
      return false
    } finally {
      lock.current = false
      setBusy(false)
    }
  }
  const placeOrder = async (shippingAddress, shippingMethod, pickupContact) => {
    if (lock.current || !uid) return null
    lock.current = true
    setBusy(true)
    try {
      const attempt = checkoutAttempt(
        uid,
        items[0]?.cartVersion,
        shippingAddress,
        shippingMethod,
        items[0]?.quoteVersion,
        pickupContact,
      )
      const order = await api.createOrder(attempt)
      clearCheckoutAttempt(uid)
      if (uid === currentUid.current) {
        ++sequence.current
        setItems([])
        setError('')
      }
      return order
    } catch (e) {
      // 401/403 can arrive after an earlier ambiguous attempt; keep its key.
      if (
        [
          'INVALID_INPUT',
          'EMPTY_CART',
          'CART_CHANGED',
          'INVALID_CART',
          'STOCK_UNAVAILABLE',
          'SHIPPING_CHANGED',
          'PRICE_CHANGED',
          'QUOTE_REQUIRED',
          'CHECKOUT_DISABLED',
          'SHIPPING_UNAVAILABLE',
        ].includes(e.data?.code)
      )
        clearCheckoutAttempt(uid)
      throw e
    } finally {
      lock.current = false
      setBusy(false)
    }
  }
  return (
    <Context.Provider
      value={{
        items: uid ? items : [],
        loading,
        busy,
        error,
        refresh,
        placeOrder,
        itemCount: uid ? items.reduce((s, i) => s + i.quantity, 0) : 0,
        total: items.reduce((s, i) => s + i.price * i.quantity, 0),
        addItem: (id, quantity = 1) =>
          mutate(() => api.addToCart(id, quantity), 'Added to your bag.'),
        updateItem: (id, quantity) =>
          mutate(() => api.updateCartItem(id, quantity), 'Bag updated.'),
        removeItem: (id) => mutate(() => api.removeFromCart(id), 'Item removed.'),
      }}
    >
      {children}
    </Context.Provider>
  )
}
export const useCart = () => useContext(Context)
