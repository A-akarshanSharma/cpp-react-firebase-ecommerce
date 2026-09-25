import test from 'node:test'
import assert from 'node:assert/strict'
import { checkoutAttempt, clearCheckoutAttempt } from '../src/services/checkoutAttempt.js'
test('checkout intent survives ambiguous failures and is scoped to the account', () => {
  const values = new Map()
  globalThis.sessionStorage = {
    getItem: (k) => values.get(k) ?? null,
    setItem: (k, v) => values.set(k, v),
    removeItem: (k) => values.delete(k),
  }
  const address = { line1: 'Original address' }
  const first = checkoutAttempt(
    'customer-a',
    'cart-v1',
    address,
    {
      id: 'shipping-1',
      feeMinor: 1250,
    },
    'reviewed-price-v1',
  )
  assert.deepEqual(first.shippingAddress, address)
  assert.equal(first.cartVersion, 'cart-v1')
  assert.equal(first.quoteVersion, 'reviewed-price-v1')
  assert.equal(first.shippingMethodId, 'shipping-1')
  assert.equal(first.shippingFeeMinor, 1250)
  assert.deepEqual(
    checkoutAttempt('customer-a', 'cart-v2', { line1: 'New address' }, undefined, 'changed-price'),
    first,
  )
  assert.notEqual(checkoutAttempt('customer-b', 'cart-v1').key, first.key)
  clearCheckoutAttempt('customer-a')
  const next = checkoutAttempt('customer-a', 'cart-v2')
  assert.notEqual(next.key, first.key)
  assert.equal(next.cartVersion, 'cart-v2')
  delete globalThis.sessionStorage
})
test('checkout fails before submission when its retry key cannot be persisted', () => {
  globalThis.sessionStorage = {
    getItem: () => null,
    setItem: () => {
      throw new Error('blocked')
    },
  }
  assert.throws(() => checkoutAttempt('customer-a', 'v1'), /session storage/)
  delete globalThis.sessionStorage
})
